/*
 *  i386 emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "cpu.h"
#include "tcg.h"
#include "atomic.h"

target_ulong virt_to_phys(target_ulong virt) {
#if (TARGET_LONG_BITS == 32)
        #define MASK2 (0xFFFFFFFF - MASK1)
        #define MASK1 (0xFFFFFFFF >> (32-TARGET_PAGE_BITS))
#elif (TARGET_LONG_BITS == 64)
        #define MASK2 (0xFFFFFFFFFFFFFFFF - MASK1)
        #define MASK1 (0xFFFFFFFFFFFFFFFF >> (64-TARGET_PAGE_BITS))
#else
    #error Unsupported TARGET_LONG_BITS
#endif
        target_ulong phys_addr = -1;
        int index = (virt >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        int i;
        for (i = 0; i < NB_MMU_MODES; i++) {
          target_ulong addr = (cpu->tlb_table[i][index].addr_code & MASK2) | (virt & (MASK1));
          if ((virt == addr)) {
            void *p = (void *)(uintptr_t)((cpu->tlb_table[i][index].addr_code & TARGET_PAGE_MASK) + cpu->tlb_table[i][index].addend);
            phys_addr = tlib_host_ptr_to_guest_offset(p);
            if (phys_addr != -1)
              phys_addr += (virt & MASK1);
            break;
          }
        }
        return phys_addr;
}

int tb_invalidated_flag;

static void TLIB_NORETURN cpu_loop_exit_without_hook(CPUState *env)
{
    env->current_tb = NULL;
    longjmp(env->jmp_env, 1);
}

void TLIB_NORETURN cpu_loop_exit(CPUState *env)
{
    if(env->block_finished_hook_present)
    {
        target_ulong pc = CPU_PC(env);
        // TODO: here we would need to have the number of executed instructions, how?!
        tlib_on_block_finished(pc, -1);
    }
    cpu_loop_exit_without_hook(env);
}

void TLIB_NORETURN cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc, uint32_t call_hook)
{
    TranslationBlock *tb;
    uint32_t executed_instructions = 0;
    if (pc) {
        tb = tb_find_pc(pc);
        if(!tb)
        {
            tlib_abortf("tb_find_pc for pc = 0x%lx failed!", pc);
        }
        executed_instructions = cpu_restore_state_and_restore_instructions_count(cpu, tb, pc);
    }
    if(call_hook && cpu->block_finished_hook_present)
    {
        tlib_on_block_finished(CPU_PC(cpu), executed_instructions);
    }

    cpu_loop_exit_without_hook(cpu);
}

static TranslationBlock *tb_find_slow(CPUState *env,
                                      target_ulong pc,
                                      target_ulong cs_base,
                                      uint64_t flags)
{
    tlib_on_translation_block_find_slow(pc);
    TranslationBlock *tb, **ptb1;
    unsigned int h;
    tb_page_addr_t phys_pc, phys_page1;
    target_ulong virt_page2;

    tb_invalidated_flag = 0;

    /* find translated block using physical mappings */
    phys_pc = get_page_addr_code(env, pc);
    phys_page1 = phys_pc & TARGET_PAGE_MASK;
    h = tb_phys_hash_func(phys_pc);
    ptb1 = &tb_phys_hash[h];

    if(unlikely(env->tb_cache_disabled)) {
        goto not_found;
    }

    for(;;) {
        tb = *ptb1;
        if (!tb)
            goto not_found;
        if (tb->pc == pc &&
            tb->page_addr[0] == phys_page1 &&
            tb->cs_base == cs_base &&
            tb->flags == flags) {
            /* check next page if needed */
            if (tb->page_addr[1] != -1) {
                tb_page_addr_t phys_page2;

		virt_page2 = (pc & TARGET_PAGE_MASK) +
                    TARGET_PAGE_SIZE;
		phys_page2 = get_page_addr_code(env, virt_page2);
                if (tb->page_addr[1] == phys_page2)
                    goto found;
            } else {
                goto found;
            }
        }
        ptb1 = &tb->phys_hash_next;
    }
 not_found:
   /* if no translated code available, then translate it now */
    tb = tb_gen_code(env, pc, cs_base, flags, size_of_next_block_to_translate);
    size_of_next_block_to_translate = 0;

 found:
    /* Move the last found TB to the head of the list */
    if (likely(*ptb1)) {
        *ptb1 = tb->phys_hash_next;
        tb->phys_hash_next = tb_phys_hash[h];
        tb_phys_hash[h] = tb;
    }
    /* we add the TB in the virtual pc hash table */
    env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)] = tb;


    return tb;
}

static inline TranslationBlock *tb_find_fast(CPUState *env)
{
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    int flags;

    /* we record a subset of the CPU state. It will
       always be the same before a given translated block
       is executed. */
    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    tb = env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)];
    if (unlikely(!tb || tb->pc != pc || tb->cs_base != cs_base ||
                 tb->flags != flags || env->tb_cache_disabled)) {
        tb = tb_find_slow(env, pc, cs_base, flags);
    }
    return tb;
}

static CPUDebugExcpHandler *debug_excp_handler;

CPUDebugExcpHandler *cpu_set_debug_excp_handler(CPUDebugExcpHandler *handler)
{
    CPUDebugExcpHandler *old_handler = debug_excp_handler;

    debug_excp_handler = handler;
    return old_handler;
}

static void verify_state(CPUState *env)
{
    if(env->atomic_memory_state == NULL)
    {
        return;
    }
    if(env->atomic_memory_state->locking_cpu_id == env->id)
    {
        clear_global_memory_lock(env);
    }
}

/* main execution loop */

int process_interrupt(int interrupt_request, CPUState *env);
void cpu_exec_prologue(CPUState *env);
void cpu_exec_epilogue(CPUState *env);

// it looks like cpu_exec is aware of possible problems and restores `env`, so the warning is not necessary
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclobbered"
int cpu_exec(CPUState *env)
{
    int ret, interrupt_request;
    TranslationBlock *tb;
    uint8_t *tc_ptr;
    uintptr_t next_tb;

    if (env->wfi) {
        if (!cpu_has_work(env)) {
            return EXCP_WFI;
        }

        env->wfi = 0;
    }

    cpu_exec_prologue(env);
    env->exception_index = -1;

    /* prepare setjmp context for exception handling */
    for(;;) {
        verify_state(env);
        if (setjmp(env->jmp_env) == 0) {
            /* if an exception is pending, we execute it here */
            if (env->exception_index >= 0) {
                if (env->return_on_exception || env->exception_index >= EXCP_INTERRUPT) {
                    /* exit request from the cpu execution loop */
                    ret = env->exception_index;
                    if ((ret == EXCP_DEBUG) && debug_excp_handler) {
                        debug_excp_handler(env);
                    }
                    break;
                } else {
                    do_interrupt(env);
                    if(env->exception_index != -1) {
                        if (env->exception_index == EXCP_WFI) {
                            env->exception_index = -1;
                            ret = 0;
                            break;
                        }
                        env->exception_index = -1;
                    }
                }
            }

            next_tb = 0; /* force lookup of first TB */
            for(;;) {
                interrupt_request = env->interrupt_request;
                if (unlikely(interrupt_request)) {
                    if (interrupt_request & CPU_INTERRUPT_DEBUG) {
                        env->interrupt_request &= ~CPU_INTERRUPT_DEBUG;
                        env->exception_index = EXCP_DEBUG;
                        cpu_loop_exit_without_hook(env);
                    }
                    if (process_interrupt(interrupt_request, env)) {
                        next_tb = 0;
                    }
                    if (env->exception_index == EXCP_WFI) {
                        cpu_loop_exit_without_hook(env);
                    }
                    env->exception_index = -1;
                    /* Don't use the cached interrupt_request value,
                       do_interrupt may have updated the EXITTB flag. */
                    if (env->interrupt_request & CPU_INTERRUPT_EXITTB) {
                        env->interrupt_request &= ~CPU_INTERRUPT_EXITTB;
                        /* ensure that no TB jump will be modified as
                           the program flow was changed */
                        next_tb = 0;
                    }
                }
                if (unlikely(env->exit_request)) {
                    env->exit_request = 0;
                    env->exception_index = EXCP_INTERRUPT;
                    cpu_loop_exit_without_hook(env);
                }
                if (unlikely(env->tb_restart_request)) {
                    env->tb_restart_request = 0;
                    cpu_loop_exit_without_hook(env);
                }
                if(unlikely(env->exception_index != -1)) {
                    cpu_loop_exit_without_hook(env);
                }

#ifdef TARGET_PROTO_ARM_M
                if(env->regs[15] >= 0xfffffff0)
                {
                    do_v7m_exception_exit(env);
                    next_tb = 0;
                }
#endif

                tb = tb_find_fast(env);
                /* Note: we do it here to avoid a gcc bug on Mac OS X when
                   doing it in tb_find_slow */
                if (tb_invalidated_flag) {
                    /* as some TB could have been invalidated because
                       of memory exceptions while generating the code, we
                       must recompute the hash index here */
                    next_tb = 0;
                    tb_invalidated_flag = 0;
                }
                /* see if we can patch the calling TB. When the TB
                   spans two pages, we cannot safely do a direct
                   jump.
                   We do not chain blocks if the chaining is explicitly disabled or if
                   there is a hook registered for the block footer. */

                if (!env->chaining_disabled && !env->block_finished_hook_present && next_tb != 0 && tb->page_addr[1] == -1) {
                    tb_add_jump((TranslationBlock *)(next_tb & ~3), next_tb & 3, tb);
                }

                /* cpu_interrupt might be called while translating the
                   TB, but before it is linked into a potentially
                   infinite loop and becomes env->current_tb. Avoid
                   starting execution if there is a pending interrupt. */
                env->current_tb = tb;
                asm volatile("" ::: "memory");
                if (likely(!env->exit_request)) {
                    tc_ptr = tb->tc_ptr;
                    /* execute the generated code */
                    next_tb = tcg_tb_exec(env, tc_ptr);
                    if ((next_tb & 3) == 2) {
                        tb = (TranslationBlock *)(uintptr_t)(next_tb & ~3);
                        /* Restore PC.  */
                        cpu_pc_from_tb(env, tb);
                        env->exception_index = EXCP_INTERRUPT;
                        next_tb = 0;
                        cpu_loop_exit_without_hook(env);
                    }
                }
                env->current_tb = NULL;
                /* reset soft MMU for next block (it can currently
                   only be set by a memory fault) */
            } /* for(;;) */
        } else {
            /* Reload env after longjmp - the compiler may have smashed all
             * local variables as longjmp is marked 'noreturn'. */
            env = cpu;
        }
    } /* for(;;) */

    cpu_exec_epilogue(env);

    return ret;
}
#pragma GCC diagnostic pop

