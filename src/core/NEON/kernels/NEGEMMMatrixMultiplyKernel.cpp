/*
 * Copyright (c) 2017 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "arm_compute/core/NEON/kernels/NEGEMMMatrixMultiplyKernel.h"

#include "arm_compute/core/AccessWindowTranspose.h"
#include "arm_compute/core/Error.h"
#include "arm_compute/core/Helpers.h"
#include "arm_compute/core/IAccessWindow.h"
#include "arm_compute/core/ITensor.h"
#include "arm_compute/core/TensorInfo.h"
#include "arm_compute/core/Types.h"
#include "arm_compute/core/Utils.h"
#include "arm_compute/core/Validate.h"
#include "arm_compute/core/Window.h"

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>
#include <tuple>

using namespace arm_compute;

namespace arm_compute
{
class Coordinates;
} // namespace arm_compute

namespace
{
template <bool multiply_alpha>
void vector_matrix_multiply_f32(const ITensor *input0, const ITensor *input1, ITensor *output, const Window &window, float alpha)
{
    const auto width_matrix_b  = static_cast<int>(output->info()->dimension(0));
    const auto in_b_stride     = static_cast<int>(input1->info()->strides_in_bytes()[1] / data_size_from_type(input1->info()->data_type()));
    const auto num_elems_vec_a = static_cast<int>(input0->info()->dimension(0));

    const int window_start_x = 16 * window.thread_id();
    const int window_step_x  = 16 * window.num_threads();
    // Make sure (window_end_x - window_start_x) is a multiple of window_step_x
    const int window_end_x = width_matrix_b + (window_step_x - ((width_matrix_b - window_start_x) % window_step_x)) % window_step_x;

    Window win_out(window);
    win_out.set(Window::DimX, Window::Dimension(window_start_x, window_end_x, window_step_x));
    win_out.set(Window::DimY, Window::Dimension(0, 1, 1));

    Window win_a(window);
    win_a.set(Window::DimX, Window::Dimension(0, 0, 0));
    win_a.set(Window::DimY, Window::Dimension(0, 0, 0));

    Window win_b;
    // Don't slice matrix B along the z dimension if matrix B has just 2 dimensions and matrix A more than 2
    // This scenario can happen when the the matrix multiplication is used to perform a convolution operation
    if(input1->info()->num_dimensions() >= 3)
    {
        win_b = window;
    }
    win_b.set(Window::DimX, Window::Dimension(window_start_x, window_end_x, window_step_x));
    win_b.set(Window::DimY, Window::Dimension(0, 1, 1));

    Iterator ina(input0, win_a);
    Iterator inb(input1, win_b);
    Iterator out(output, win_out);

    const auto vec_a = reinterpret_cast<const float *>(ina.ptr());

    execute_window_loop(win_out, [&](const Coordinates & id)
    {
        if(id.x() > width_matrix_b)
        {
            return;
        }

        float32x4_t acc0 = vdupq_n_f32(0.f);
        float32x4_t acc1 = vdupq_n_f32(0.f);
        float32x4_t acc2 = vdupq_n_f32(0.f);
        float32x4_t acc3 = vdupq_n_f32(0.f);

        const auto matrix_b = reinterpret_cast<const float *>(inb.ptr());

        int i = 0;
        for(; i <= (num_elems_vec_a - 4); i += 4)
        {
            const float32x2_t a0l = vld1_f32(&vec_a[i]);
            const float32x2_t a0h = vld1_f32(&vec_a[i] + 2);

            const float32x4_t b00 = vld1q_f32(&matrix_b[0 + (i + 0) * in_b_stride]);
            const float32x4_t b01 = vld1q_f32(&matrix_b[4 + (i + 0) * in_b_stride]);
            const float32x4_t b02 = vld1q_f32(&matrix_b[8 + (i + 0) * in_b_stride]);
            const float32x4_t b03 = vld1q_f32(&matrix_b[12 + (i + 0) * in_b_stride]);

            const float32x4_t b10 = vld1q_f32(&matrix_b[0 + (i + 1) * in_b_stride]);
            const float32x4_t b11 = vld1q_f32(&matrix_b[4 + (i + 1) * in_b_stride]);
            const float32x4_t b12 = vld1q_f32(&matrix_b[8 + (i + 1) * in_b_stride]);
            const float32x4_t b13 = vld1q_f32(&matrix_b[12 + (i + 1) * in_b_stride]);

            const float32x4_t b20 = vld1q_f32(&matrix_b[0 + (i + 2) * in_b_stride]);
            const float32x4_t b21 = vld1q_f32(&matrix_b[4 + (i + 2) * in_b_stride]);
            const float32x4_t b22 = vld1q_f32(&matrix_b[8 + (i + 2) * in_b_stride]);
            const float32x4_t b23 = vld1q_f32(&matrix_b[12 + (i + 2) * in_b_stride]);

            const float32x4_t b30 = vld1q_f32(&matrix_b[0 + (i + 3) * in_b_stride]);
            const float32x4_t b31 = vld1q_f32(&matrix_b[4 + (i + 3) * in_b_stride]);
            const float32x4_t b32 = vld1q_f32(&matrix_b[8 + (i + 3) * in_b_stride]);
            const float32x4_t b33 = vld1q_f32(&matrix_b[12 + (i + 3) * in_b_stride]);

            acc0 = vmlaq_lane_f32(acc0, b00, a0l, 0);
            acc1 = vmlaq_lane_f32(acc1, b01, a0l, 0);
            acc2 = vmlaq_lane_f32(acc2, b02, a0l, 0);
            acc3 = vmlaq_lane_f32(acc3, b03, a0l, 0);

            acc0 = vmlaq_lane_f32(acc0, b10, a0l, 1);
            acc1 = vmlaq_lane_f32(acc1, b11, a0l, 1);
            acc2 = vmlaq_lane_f32(acc2, b12, a0l, 1);
            acc3 = vmlaq_lane_f32(acc3, b13, a0l, 1);

            acc0 = vmlaq_lane_f32(acc0, b20, a0h, 0);
            acc1 = vmlaq_lane_f32(acc1, b21, a0h, 0);
            acc2 = vmlaq_lane_f32(acc2, b22, a0h, 0);
            acc3 = vmlaq_lane_f32(acc3, b23, a0h, 0);

            acc0 = vmlaq_lane_f32(acc0, b30, a0h, 1);
            acc1 = vmlaq_lane_f32(acc1, b31, a0h, 1);
            acc2 = vmlaq_lane_f32(acc2, b32, a0h, 1);
            acc3 = vmlaq_lane_f32(acc3, b33, a0h, 1);
        }

        for(; i < num_elems_vec_a; i++)
        {
            const float a0 = vec_a[i];

            const float32x4_t b00 = vld1q_f32(&matrix_b[0 + i * in_b_stride]);
            const float32x4_t b01 = vld1q_f32(&matrix_b[4 + i * in_b_stride]);
            const float32x4_t b02 = vld1q_f32(&matrix_b[8 + i * in_b_stride]);
            const float32x4_t b03 = vld1q_f32(&matrix_b[12 + i * in_b_stride]);

            acc0 = vmlaq_n_f32(acc0, b00, a0);
            acc1 = vmlaq_n_f32(acc1, b01, a0);
            acc2 = vmlaq_n_f32(acc2, b02, a0);
            acc3 = vmlaq_n_f32(acc3, b03, a0);
        }

        // Multiply by the weight of matrix product (alpha)
        if(multiply_alpha)
        {
            const float32x4_t alpha_f32 = vdupq_n_f32(alpha);
            acc0                        = vmulq_f32(acc0, alpha_f32);
            acc1                        = vmulq_f32(acc1, alpha_f32);
            acc2                        = vmulq_f32(acc2, alpha_f32);
            acc3                        = vmulq_f32(acc3, alpha_f32);
        }

        const auto vec_out = reinterpret_cast<float *>(out.ptr());

        vst1q_f32(&vec_out[0], acc0);
        vst1q_f32(&vec_out[4], acc1);
        vst1q_f32(&vec_out[8], acc2);
        vst1q_f32(&vec_out[12], acc3);
    },
    inb, out);
}

template <bool multiply_alpha>
void matrix_matrix_multiply_f32(const ITensor *input0, const ITensor *input1, ITensor *output, const Window &window, float alpha)
{
    const size_t in_b_stride          = input1->info()->strides_in_bytes()[1] / data_size_from_type(input1->info()->data_type());
    const size_t out_stride1          = output->info()->strides_in_bytes()[1] / data_size_from_type(output->info()->data_type());
    const size_t out_stride2          = out_stride1 * 2;
    const size_t out_stride3          = out_stride1 * 3;
    const int    num_elems_matrix_b_x = input1->info()->dimension(0);

    // Set step_x and step_y for matrix A. Scale by a factor of 4 the Y range as the input interleaved matrix A has 4 times less the rows of the output matrix
    Window win_a(window);
    win_a.set(Window::DimX, Window::Dimension(0, 0, 0));
    win_a.set(Window::DimY, Window::Dimension(window.y().start() / 4, std::max(window.y().end() / 4, 1), 1));

    Window win_b;
    // Don't slice matrix B along the z dimension if matrix B has just 2 dimensions and matrix A more than 2
    // This scenario can happen when the the matrix multiplication is used to perform a convolution operation
    if(input1->info()->num_dimensions() >= 3)
    {
        win_b = window;
    }
    // Set step_x and step_y for matrix B. Scale by a factor of 4 the X range as the input transposed matrix A has 4 times less the cols of the output matrix
    // The step along the x direction is 4 times the in_b_stride because for each iteration we compute 4 blocks of size 4x4
    win_b.set(Window::DimX, Window::Dimension(window.x().start() / 4, window.x().end() / 4, 4 * in_b_stride));
    win_b.set(Window::DimY, Window::Dimension(0, 1, 0));

    Iterator ina(input0, win_a);
    Iterator inb(input1, win_b);
    Iterator out(output, window);

    // The implementation assumes that the matrix A and Matrix B have been reshaped respectively with NEGEMMInterleave4x4 and NEGEMMTranspose1xW
    // The reshaping of the matrices helps to have a cache friendly implementation and helps to avoid the data re-arrangements needed for computing 16x4 elements per iteration
    // All the values needed for computing a single 4x4 block will be read from consecutive memory positions
    execute_window_loop(window, [&](const Coordinates & id)
    {
        auto mtx_a0 = reinterpret_cast<const float *>(ina.ptr());
        auto mtx_b0 = reinterpret_cast<const float *>(inb.ptr());
        auto mtx_b1 = mtx_b0 + in_b_stride;
        auto mtx_b2 = mtx_b1 + in_b_stride;
        auto mtx_b3 = mtx_b2 + in_b_stride;

        float32x4_t acc00 = vdupq_n_f32(0.f);
        float32x4_t acc10 = vdupq_n_f32(0.f);
        float32x4_t acc20 = vdupq_n_f32(0.f);
        float32x4_t acc30 = vdupq_n_f32(0.f);

        float32x4_t acc01 = vdupq_n_f32(0.f);
        float32x4_t acc11 = vdupq_n_f32(0.f);
        float32x4_t acc21 = vdupq_n_f32(0.f);
        float32x4_t acc31 = vdupq_n_f32(0.f);

        float32x4_t acc02 = vdupq_n_f32(0.f);
        float32x4_t acc12 = vdupq_n_f32(0.f);
        float32x4_t acc22 = vdupq_n_f32(0.f);
        float32x4_t acc32 = vdupq_n_f32(0.f);

        float32x4_t acc03 = vdupq_n_f32(0.f);
        float32x4_t acc13 = vdupq_n_f32(0.f);
        float32x4_t acc23 = vdupq_n_f32(0.f);
        float32x4_t acc33 = vdupq_n_f32(0.f);

        for(int k = 0; k < num_elems_matrix_b_x; k += 4)
        {
            const float32x4_t a    = vld1q_f32(mtx_a0);
            const float32x2_t a00l = vget_low_f32(a);
            const float32x2_t a00h = vget_high_f32(a);
            const float32x4_t b00  = vld1q_f32(mtx_b0);
            const float32x4_t b10  = vld1q_f32(mtx_b1);
            const float32x4_t b20  = vld1q_f32(mtx_b2);
            const float32x4_t b30  = vld1q_f32(mtx_b3);

            // 4x4 block 0
            acc00 = vmlaq_lane_f32(acc00, b00, a00l, 0);
            acc10 = vmlaq_lane_f32(acc10, b00, a00l, 1);
            acc20 = vmlaq_lane_f32(acc20, b00, a00h, 0);
            acc30 = vmlaq_lane_f32(acc30, b00, a00h, 1);

            // 4x4 block 1
            acc01 = vmlaq_lane_f32(acc01, b10, a00l, 0);
            acc11 = vmlaq_lane_f32(acc11, b10, a00l, 1);
            acc21 = vmlaq_lane_f32(acc21, b10, a00h, 0);
            acc31 = vmlaq_lane_f32(acc31, b10, a00h, 1);

            // 4x4 block 2
            acc02 = vmlaq_lane_f32(acc02, b20, a00l, 0);
            acc12 = vmlaq_lane_f32(acc12, b20, a00l, 1);
            acc22 = vmlaq_lane_f32(acc22, b20, a00h, 0);
            acc32 = vmlaq_lane_f32(acc32, b20, a00h, 1);

            // 4x4 block 3
            acc03 = vmlaq_lane_f32(acc03, b30, a00l, 0);
            acc13 = vmlaq_lane_f32(acc13, b30, a00l, 1);
            acc23 = vmlaq_lane_f32(acc23, b30, a00h, 0);
            acc33 = vmlaq_lane_f32(acc33, b30, a00h, 1);

            mtx_a0 += 4;
            mtx_b0 += 4;
            mtx_b1 += 4;
            mtx_b2 += 4;
            mtx_b3 += 4;
        }

        // Multiply by the weight of matrix product (alpha)
        if(multiply_alpha)
        {
            const float32x4_t alpha_f32 = vdupq_n_f32(alpha);
            acc00                       = vmulq_f32(acc00, alpha_f32);
            acc10                       = vmulq_f32(acc10, alpha_f32);
            acc20                       = vmulq_f32(acc20, alpha_f32);
            acc30                       = vmulq_f32(acc30, alpha_f32);
            acc01                       = vmulq_f32(acc01, alpha_f32);
            acc11                       = vmulq_f32(acc11, alpha_f32);
            acc21                       = vmulq_f32(acc21, alpha_f32);
            acc31                       = vmulq_f32(acc31, alpha_f32);
            acc02                       = vmulq_f32(acc02, alpha_f32);
            acc12                       = vmulq_f32(acc12, alpha_f32);
            acc22                       = vmulq_f32(acc22, alpha_f32);
            acc32                       = vmulq_f32(acc32, alpha_f32);
            acc03                       = vmulq_f32(acc03, alpha_f32);
            acc13                       = vmulq_f32(acc13, alpha_f32);
            acc23                       = vmulq_f32(acc23, alpha_f32);
            acc33                       = vmulq_f32(acc33, alpha_f32);
        }

        const auto mtx_out0 = reinterpret_cast<float *>(out.ptr());
        const auto mtx_out1 = mtx_out0 + 4;
        const auto mtx_out2 = mtx_out1 + 4;
        const auto mtx_out3 = mtx_out2 + 4;

        // Store the 4 blocks
        vst1q_f32(mtx_out0, acc00);
        vst1q_f32(mtx_out1, acc01);
        vst1q_f32(mtx_out2, acc02);
        vst1q_f32(mtx_out3, acc03);
        vst1q_f32(mtx_out0 + out_stride1, acc10);
        vst1q_f32(mtx_out1 + out_stride1, acc11);
        vst1q_f32(mtx_out2 + out_stride1, acc12);
        vst1q_f32(mtx_out3 + out_stride1, acc13);
        vst1q_f32(mtx_out0 + out_stride2, acc20);
        vst1q_f32(mtx_out1 + out_stride2, acc21);
        vst1q_f32(mtx_out2 + out_stride2, acc22);
        vst1q_f32(mtx_out3 + out_stride2, acc23);
        vst1q_f32(mtx_out0 + out_stride3, acc30);
        vst1q_f32(mtx_out1 + out_stride3, acc31);
        vst1q_f32(mtx_out2 + out_stride3, acc32);
        vst1q_f32(mtx_out3 + out_stride3, acc33);
    },
    ina, inb, out);
}

template <bool multiply_alpha>
void matrix_matrix_multiply_f16(const ITensor *input0, const ITensor *input1, ITensor *output, const Window &window, float alpha)
{
#ifdef ARM_COMPUTE_ENABLE_FP16
    const size_t in_b_stride = input1->info()->strides_in_bytes()[1] / data_size_from_type(input1->info()->data_type());
    const size_t out_stride  = output->info()->strides_in_bytes()[1] / data_size_from_type(output->info()->data_type());

    // Set step_x and step_y for matrix A. Scale by a factor of 4 the Y range as the input interleaved matrix A has 4 times less the rows of the output matrix
    Window win_a(window);
    win_a.set(Window::DimX, Window::Dimension(0, 0, 0));
    win_a.set(Window::DimY, Window::Dimension(window.y().start() / 4, std::max(window.y().end() / 4, 1), 1));

    Window win_b;
    // Don't slice matrix B along the z dimension if matrix B has just 2 dimensions and matrix A more than 2
    // This scenario can happen when the the matrix multiplication is used to perform a convolution operation
    if(input1->info()->num_dimensions() >= 3)
    {
        win_b = window;
    }
    // Set step_x and step_y for matrix B. Scale by a factor of 8 the X range as the input transposed matrix A has 8 times less the cols of the output matrix
    win_b.set(Window::DimX, Window::Dimension(window.x().start() / 8, window.x().end() / 8, in_b_stride));
    win_b.set(Window::DimY, Window::Dimension(0, 1, 0));

    Iterator ina(input0, win_a);
    Iterator inb(input1, win_b);
    Iterator out(output, window);

    // Number of iterations of inner loop. Since 8 is the number of accumulations per loop, num_it = (width_mtx_b / 4) / 8
    const size_t num_it = ((input1->info()->dimension(0)) >> 2) >> 3;

    const float16x8_t alpha_f16 = vdupq_n_f16(alpha);

    execute_window_loop(window, [&](const Coordinates & id)
    {
        const auto   *mtx_a0  = reinterpret_cast<const float16_t *>(ina.ptr());
        const auto   *mtx_b0  = reinterpret_cast<const float16_t *>(inb.ptr());
        auto         *mtx_out = reinterpret_cast<float16_t *>(out.ptr());
        float16x8x4_t c =
        {
            {
                vdupq_n_f16(0.f),
                vdupq_n_f16(0.f),
                vdupq_n_f16(0.f),
                vdupq_n_f16(0.f)
            }
        };

        /*
        This kernel puts the values in a 4x4 block of Matrix A on the same row (Interleaved values)
             |a00 a01 a02 a03 | a04 a05 a06 a07|
             |a10 a11 a12 a13 | a14 a15 a16 a17|
             |a20 a21 a22 a23 | a24 a25 a26 a27| = | a00 a10 a20 a30 || a01 a11 a21 a31 || a02 a12 a22 a32 || a03 a13 a23 a33 | a40 a50 a60 a70 | ...
             |a30 a31 a32 a33 | a34 a35 a36 a37|   | a04 a14 a24 a34 || a05 a15 a25 a35 || a06 a15 a26 a36 || a07 a17 a27 a37 | a44 a54 a64 a74 | ...
             |a40 a41 a42 a43 | a44 a45 a46 a47|
             |a50 a51 a52 a53 | a54 a55 a56 a57|
             |a60 a61 a62 a63 | a64 a65 a66 a67|
             |a70 a71 a72 a73 | a74 a75 a76 a77|

             After this operation, the output matrix will have the following shape: [ height * 4, width / 4 ]

        B Matrix has been transposed as shown below

           |b00 b01 b02 b03 b04 b05 b06 b07|
           |b10 b11 b12 b13 b14 b15 b16 b17|
           |b20 b21 b22 b23 b24 b25 b26 b27|
           |b30 b31 b32 b33 b34 b35 b36 b37|
          ------------------->

           |b00 b01 b02 b03 b04 b05 b06 b07||b10 b11 b12 b13 b14 b15 b16 b17||b20 b21 b22 b23 b24 b25 b26 b27||b30 b31 b32 b33 b34 b35 b36 b37|

            c.val[0][0] = a00*b00 + a01*b10 + a02*b20 + a03*b30
            c.val[0][1] = a00*b01 + a01*b11 + a02*b21 + a03*b31

        The size of the output tensor's XY-plane must be the following shape [ width * 8, height / 8 ]. All other dimensions must have the same size.
        */
        for(size_t k = num_it; k > 0; mtx_a0 += 16, mtx_b0 += 32, --k)
        {
            const float16x8_t p00 = vld1q_f16(mtx_a0);
            const float16x8_t p02 = vld1q_f16(mtx_a0 + 8);
            const float16x8_t q00 = vld1q_f16(mtx_b0);
            const float16x8_t q02 = vld1q_f16(mtx_b0 + 8);
            const float16x8_t q04 = vld1q_f16(mtx_b0 + 16);
            const float16x8_t q06 = vld1q_f16(mtx_b0 + 24);

            c.val[0] = vaddq_f16(c.val[0], vmulq_n_f16(q00, vgetq_lane_f16(p00, 0)));
            c.val[1] = vaddq_f16(c.val[1], vmulq_n_f16(q00, vgetq_lane_f16(p00, 1)));
            c.val[2] = vaddq_f16(c.val[2], vmulq_n_f16(q00, vgetq_lane_f16(p00, 2)));
            c.val[3] = vaddq_f16(c.val[3], vmulq_n_f16(q00, vgetq_lane_f16(p00, 3)));

            c.val[0] = vaddq_f16(c.val[0], vmulq_n_f16(q02, vgetq_lane_f16(p00, 4)));
            c.val[1] = vaddq_f16(c.val[1], vmulq_n_f16(q02, vgetq_lane_f16(p00, 5)));
            c.val[2] = vaddq_f16(c.val[2], vmulq_n_f16(q02, vgetq_lane_f16(p00, 6)));
            c.val[3] = vaddq_f16(c.val[3], vmulq_n_f16(q02, vgetq_lane_f16(p00, 7)));

            c.val[0] = vaddq_f16(c.val[0], vmulq_n_f16(q04, vgetq_lane_f16(p02, 0)));
            c.val[1] = vaddq_f16(c.val[1], vmulq_n_f16(q04, vgetq_lane_f16(p02, 1)));
            c.val[2] = vaddq_f16(c.val[2], vmulq_n_f16(q04, vgetq_lane_f16(p02, 2)));
            c.val[3] = vaddq_f16(c.val[3], vmulq_n_f16(q04, vgetq_lane_f16(p02, 3)));

            c.val[0] = vaddq_f16(c.val[0], vmulq_n_f16(q06, vgetq_lane_f16(p02, 4)));
            c.val[1] = vaddq_f16(c.val[1], vmulq_n_f16(q06, vgetq_lane_f16(p02, 5)));
            c.val[2] = vaddq_f16(c.val[2], vmulq_n_f16(q06, vgetq_lane_f16(p02, 6)));
            c.val[3] = vaddq_f16(c.val[3], vmulq_n_f16(q06, vgetq_lane_f16(p02, 7)));
        }

        if(multiply_alpha)
        {
            c.val[0] = vmulq_f16(c.val[0], alpha_f16);
            c.val[1] = vmulq_f16(c.val[1], alpha_f16);
            c.val[2] = vmulq_f16(c.val[2], alpha_f16);
            c.val[3] = vmulq_f16(c.val[3], alpha_f16);
        }

        vst1q_f16(mtx_out + 0 * out_stride, c.val[0]);
        vst1q_f16(mtx_out + 1 * out_stride, c.val[1]);
        vst1q_f16(mtx_out + 2 * out_stride, c.val[2]);
        vst1q_f16(mtx_out + 3 * out_stride, c.val[3]);
    },
    ina, inb, out);
#else
    ARM_COMPUTE_ERROR("Not implemented");
#endif
}
} // namespace

NEGEMMMatrixMultiplyKernel::NEGEMMMatrixMultiplyKernel()
    : _input0(nullptr), _input1(nullptr), _output(nullptr), _alpha(1.0f)
{
}

void NEGEMMMatrixMultiplyKernel::configure(const ITensor *input0, const ITensor *input1, ITensor *output, float alpha)
{
    ARM_COMPUTE_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(input0, 1, DataType::F16, DataType::F32);
    ARM_COMPUTE_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(input1, 1, DataType::F16, DataType::F32);
    ARM_COMPUTE_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(output, 1, DataType::F16, DataType::F32);
    ARM_COMPUTE_ERROR_ON_MISMATCHING_DATA_TYPES(input0, input1, output);
    if(output->info()->dimension(1) == 1)
    {
        ARM_COMPUTE_ERROR_ON(input0->info()->dimension(0) != input1->info()->dimension(1));
    }

    _input0 = input0;
    _input1 = input1;
    _output = output;
    _alpha  = alpha;

    unsigned int       num_elems_processed_per_iteration_x = 0;
    const unsigned int num_elems_processed_per_iteration_y = 4;

    // Check if the output tensor is a vector and the data type is F32. If so,the kernel runs the vector-matrix multiplication
    if((output->info()->dimension(1) == 1) && (input0->info()->data_type() == DataType::F32))
    {
        num_elems_processed_per_iteration_x = 16;

        // Configure kernel window
        Window win = calculate_max_window(*output->info(), Steps(num_elems_processed_per_iteration_x));

        AccessWindowHorizontal output_access(output->info(), 0, num_elems_processed_per_iteration_x);

        update_window_and_padding(win,
                                  AccessWindowHorizontal(input0->info(), 0, num_elems_processed_per_iteration_x),
                                  AccessWindowHorizontal(input1->info(), 0, num_elems_processed_per_iteration_x),
                                  output_access);

        output_access.set_valid_region(win, ValidRegion(Coordinates(0, 0), output->info()->tensor_shape()));

        INEKernel::configure(win);
    }
    else
    {
        switch(input0->info()->data_type())
        {
            case DataType::F16:
            {
                num_elems_processed_per_iteration_x = 8;
                break;
            }
            case DataType::F32:
            {
                num_elems_processed_per_iteration_x = 16;
                break;
            }
            default:
            {
                ARM_COMPUTE_ERROR("Data type not supported");
                break;
            }
        }

        // Configure kernel window
        Window win = calculate_max_window(*output->info(), Steps(num_elems_processed_per_iteration_x, num_elems_processed_per_iteration_y));

        AccessWindowRectangle output_access(output->info(), 0, 0, num_elems_processed_per_iteration_x, num_elems_processed_per_iteration_y);

        update_window_and_padding(win,
                                  AccessWindowRectangle(input0->info(), 0, 0, 4, 1, 1.f, 0.25f),
                                  AccessWindowTranspose(input1->info(), 0, 0, 4, 1, 0.f, 0.25f),
                                  output_access);

        output_access.set_valid_region(win, ValidRegion(Coordinates(0, 0), output->info()->tensor_shape()));

        INEKernel::configure(win);
    }
}

void NEGEMMMatrixMultiplyKernel::run(const Window &window)
{
    ARM_COMPUTE_ERROR_ON_UNCONFIGURED_KERNEL(this);
    ARM_COMPUTE_ERROR_ON_INVALID_SUBWINDOW(INEKernel::window(), window);

    bool multiply_alpha = std::abs(1.0f - _alpha) > 0.00001f;

    // Check if the output tensor is a vector and the data type is F32. If so,the kernel runs the vector-matrix multiplication
    if((_output->info()->dimension(1) == 1) && (_input0->info()->data_type() == DataType::F32))
    {
        if(multiply_alpha)
        {
            vector_matrix_multiply_f32<true>(_input0, _input1, _output, window, _alpha);
        }
        else
        {
            vector_matrix_multiply_f32<false>(_input0, _input1, _output, window, _alpha);
        }
    }
    else
    {
        switch(_input0->info()->data_type())
        {
            case DataType::F16:
            {
                if(multiply_alpha)
                {
                    matrix_matrix_multiply_f16<true>(_input0, _input1, _output, window, _alpha);
                }
                else
                {
                    matrix_matrix_multiply_f16<false>(_input0, _input1, _output, window, _alpha);
                }
                break;
            }
            case DataType::F32:
            {
                if(multiply_alpha)
                {
                    matrix_matrix_multiply_f32<true>(_input0, _input1, _output, window, _alpha);
                }
                else
                {
                    matrix_matrix_multiply_f32<false>(_input0, _input1, _output, window, _alpha);
                }
                break;
            }
            default:
            {
                ARM_COMPUTE_ERROR("Data type not supported");
                break;
            }
        }
    }
}
