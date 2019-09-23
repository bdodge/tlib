#ifndef ARCH_EXPORTS_H_
#define ARCH_EXPORTS_H_

#include <stdint.h>

void tlib_allow_feature(uint32_t feature_bit);

uint32_t tlib_is_feature_enabled(uint32_t feature_bit);

uint32_t tlib_is_feature_allowed(uint32_t feature_bit);

void tlib_set_privilege_architecture_1_09(uint32_t enable);

void tlib_set_nmi_vector(uint32_t nmi_vector_adress, uint32_t nmi_vector_lenght);

void tlib_set_nmi(int32_t nmi, int32_t state);

#endif
