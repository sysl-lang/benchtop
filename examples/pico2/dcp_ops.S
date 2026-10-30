// The SDK's canned DCP sequences (hardware/dcp_canned.inc.S), each wrapped as a callable function for
// dcp.c: the single-precision ones, which pico_double does not reach, the fast double divide and
// square root, the classify, the raw comparison word, and every conversion.

.syntax unified
.cpu cortex-m33
.thumb

#include "hardware/dcp_instr.inc.S"
#include "hardware/dcp_canned.inc.S"

.macro fn name
.global \name
.type \name, %function
.thumb_func
\name:
.endm

.text

// uint32_t t_fadd(uint32_t x, uint32_t y), and the other single-precision operations.
fn t_fadd
  dcp_fadd_m r0,r0,r1
  bx lr

fn t_fsub
  dcp_fsub_m r0,r0,r1
  bx lr

fn t_fmul
  dcp_fmul_m r0,r0,r1,r0,r1
  bx lr

fn t_fdiv
  dcp_fdiv_m r0,r0,r1,r0,r1,r2,r3
  bx lr

fn t_fdiv_fast
  dcp_fdiv_fast_m r0,r0,r1,r0,r1,r2
  bx lr

fn t_fsqrt
  dcp_fsqrt_m r0,r0,r0,r1,r2,r3
  bx lr

fn t_fsqrt_fast
  dcp_fsqrt_fast_m r0,r0,r0,r1,r2,r3
  bx lr

// uint32_t t_fcmp(uint32_t x, uint32_t y): RCMP's word.
fn t_fcmp
  dcp_fcmp_m r0,r0,r1
  bx lr

// uint64_t t_fxprod(uint32_t x, uint32_t y): the exact product as a double.
fn t_fxprod
  dcp_fxprod_m r0,r1,r0,r1,r2,r3
  bx lr

// uint32_t t_dcmp(uint64_t x, uint64_t y): RCMP's word.
fn t_dcmp
  dcp_dcmp_m r0,r0,r1,r2,r3
  bx lr

// uint32_t t_dclassify(uint64_t x): RXVD's word.
fn t_dclassify
  dcp_dclassify_m r0,r0,r1
  bx lr

// uint64_t t_ddiv_fast(uint64_t x, uint64_t y) and t_dsqrt_fast(uint64_t x).
fn t_ddiv_fast
  dcp_ddiv_fast_m r0,r1,r0,r1,r2,r3,r0,r1,r2,r3,r12
  bx lr

fn t_dsqrt_fast
  dcp_dsqrt_fast_m r0,r1,r0,r1,r0,r1,r2,r3,r12
  bx lr

// The conversions.
fn t_f2d
  dcp_float2double_m r0,r1,r0
  bx lr

fn t_d2f
  dcp_double2float_m r0,r0,r1
  bx lr

fn t_i2d
  dcp_int2double_m r0,r1,r0
  bx lr

fn t_ui2d
  dcp_uint2double_m r0,r1,r0
  bx lr

fn t_i2f
  dcp_int2float_m r0,r0
  bx lr

fn t_ui2f
  dcp_uint2float_m r0,r0
  bx lr

fn t_d2i
  dcp_double2int_m r0,r0,r1
  bx lr

fn t_d2ui
  dcp_double2uint_m r0,r0,r1
  bx lr

fn t_d2i_r
  dcp_double2int_r_m r0,r0,r1
  bx lr

fn t_d2ui_r
  dcp_double2uint_r_m r0,r0,r1
  bx lr

fn t_f2i
  dcp_float2int_m r0,r0
  bx lr

fn t_f2ui
  dcp_float2uint_m r0,r0
  bx lr

fn t_f2i_r
  dcp_float2int_r_m r0,r0
  bx lr

fn t_f2ui_r
  dcp_float2uint_r_m r0,r0
  bx lr

// uint32_t t_pcmp(void): PCMP, whose N bit is the engaged flag, without changing it.
fn t_pcmp
  PCMP r0
  bx lr
