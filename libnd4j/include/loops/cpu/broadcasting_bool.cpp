/*******************************************************************************
 * Copyright (c) 2015-2018 Skymind, Inc.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License, Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

//
//  @author raver119@gmail.com
//

#include <op_boilerplate.h>
#include <loops/broadcasting_bool.h>
#include <loops/legacy_ops.h>
#include <types/types.h>
#include <helpers/ConstantTadHelper.h>

using namespace simdOps;

namespace functions {
    namespace broadcast {

        template <typename X, typename Y>
        void BroadcastBool<X, Y>::exec(const int opNum,
                             void *x,
                             Nd4jLong *xShapeInfo,
                             void *y,
                             Nd4jLong *yShapeInfo,
                             void *z,
                             Nd4jLong *zShapeInfo,
                             int *dimension,
                             int dimensionLength,
                             Nd4jLong *xTadShapeInfo,
                             Nd4jLong *xTadOffset,
                             Nd4jLong *zTadShapeInfo,
                             Nd4jLong *zTadOffset) {
            DISPATCH_BY_OPNUM_TT(exec, PARAMS(x,
                                               xShapeInfo,
                                               y,
                                               yShapeInfo,
                                               z,
                                               zShapeInfo,
                                               dimension,
                                               dimensionLength,
                                               xTadShapeInfo,
                                               xTadOffset,
                                               zTadShapeInfo,
                                               zTadOffset), BROADCAST_BOOL_OPS);
        }

        template <typename X, typename Y>
        void BroadcastBool<X, Y>::execInverse(const int opNum,
                             void *x,
                             Nd4jLong *xShapeInfo,
                             void *y,
                             Nd4jLong *yShapeInfo,
                             void *z,
                             Nd4jLong *zShapeInfo,
                             int *dimension,
                             int dimensionLength,
                             Nd4jLong *xTadShapeInfo,
                             Nd4jLong *xTadOffset,
                             Nd4jLong *zTadShapeInfo,
                             Nd4jLong *zTadOffset) {
            DISPATCH_BY_OPNUM_TT(execInverse, PARAMS(x,
                                               xShapeInfo,
                                               y,
                                               yShapeInfo,
                                               z,
                                               zShapeInfo,
                                               dimension,
                                               dimensionLength,
                                               xTadShapeInfo,
                                               xTadOffset,
                                               zTadShapeInfo,
                                               zTadOffset), BROADCAST_BOOL_OPS);
        }

        template <typename X, typename Z>
        template<typename OpType>
        void BroadcastBool<X, Z>::exec(void *vx,
                             Nd4jLong *xShapeInfo,
                             void *vy,
                             Nd4jLong *yShapeInfo,
                             void *vz,
                             Nd4jLong *zShapeInfo,
                             int *dimension,
                             int dimensionLength,
                             Nd4jLong *xTadShapeInfo,
                             Nd4jLong *xTadOffset,
                             Nd4jLong *zTadShapeInfo,
                             Nd4jLong *zTadOffset) {

                auto x = reinterpret_cast<X *>(vx);
                auto y = reinterpret_cast<X *>(vy);
                auto z = reinterpret_cast<Z *>(vz);

                //decompose in to several sub tads after
                //moving all dimensions (in sorted order)
                //to the back.
                //permuted version of the x shape info for setting up the tad problem
                auto xTadShapeShapeInfo = xTadShapeInfo;
                auto tadOffsets = xTadOffset;

                if (xTadShapeInfo == nullptr || tadOffsets == nullptr) {
                    auto tadPack = nd4j::ConstantTadHelper::getInstance()->tadForDimensions(xShapeInfo, dimension, dimensionLength);

                    xTadShapeShapeInfo = tadPack.primaryShapeInfo();
                    tadOffsets = tadPack.primaryOffsets();
                }

                //int *resultStride = shape::stride(xTadShapeShapeInfo);
                unsigned int tadLength = shape::length(xTadShapeShapeInfo);
                unsigned int tads = shape::length(xShapeInfo) / tadLength;

                if (zTadShapeInfo == nullptr) {
                    zTadShapeInfo = xTadShapeShapeInfo;
                    zTadOffset = tadOffsets;
                }                                

                auto lenZ = shape::length(zTadShapeInfo);
                auto lenY = shape::length(yShapeInfo);

                int tadsPerThread = tads / TAD_THRESHOLD;
                int threads = nd4j::math::nd4j_max<int>(1, tadsPerThread);
                threads = nd4j::math::nd4j_min<int>(threads, omp_get_max_threads());

                auto xEws = shape::elementWiseStride(xTadShapeShapeInfo);
                auto yEws = shape::elementWiseStride(yShapeInfo);
                auto zEws = shape::elementWiseStride(zTadShapeInfo);

                if (shape::order(xTadShapeShapeInfo) == shape::order(yShapeInfo) && shape::order(zTadShapeInfo) == shape::order(yShapeInfo) && xEws > 0 && yEws > 0 && zEws > 0) {

                    if (xEws == 1 && yEws == 1 && zEws == 1) {
                        PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                        for (int i = 0; i < tads; i++) {
                            auto oX = x + tadOffsets[i];
                            auto oZ = z + zTadOffset[i];

                            PRAGMA_OMP_SIMD
                            for (unsigned int f = 0; f < tadLength; f++)
                                oZ[f] = OpType::op(oX[f], y[f]);
                        }
                    } else {
                        PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                        for (int i = 0; i < tads; i++) {
                            auto oX = x + tadOffsets[i];
                            auto oZ = z + zTadOffset[i];

                            PRAGMA_OMP_SIMD
                            for (unsigned int f = 0; f < tadLength; f++)
                                oZ[f * zEws] = OpType::op(oX[f * xEws], y[f * yEws]);
                        }
                    }
                } else if(shape::haveSameOffsets(xTadShapeShapeInfo, yShapeInfo) && shape::haveSameOffsets(xTadShapeShapeInfo, zTadShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oX = x + tadOffsets[i];
                                        
                        // TODO: cover this codebranch with tests
                        // all this stuff already happens within thread
                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto offset = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastX);
                            oZ[offset] = OpType::op(oX[offset], y[offset]);
                        }
                    }
                }
                else if(shape::haveSameOffsets(xTadShapeShapeInfo, yShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint tadShapeInfoZCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);
                    bool canCastZ = nd4j::DataTypeUtils::castShapeInfo(zTadShapeInfo, tadShapeInfoZCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oX = x + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto offset  = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastX);
                            auto zOffset = shape::indexOffset(f, zTadShapeInfo, tadShapeInfoZCast, lenZ, canCastZ);
                            oZ[zOffset] = OpType::op(oX[offset], y[offset]);
                        }
                    }
                }
                else if(shape::haveSameOffsets(xTadShapeShapeInfo, zTadShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint yShapeInfoCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(yShapeInfo, yShapeInfoCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oX = x + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto offset  = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastX);
                            auto yOffset = shape::indexOffset(f, yShapeInfo, yShapeInfoCast, lenY, canCastY);
                            oZ[offset] = OpType::op(oX[offset], y[yOffset]);
                        }
                    }
                }
                else if(shape::haveSameOffsets(yShapeInfo, zTadShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint yShapeInfoCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(yShapeInfo, yShapeInfoCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oX = x + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto xOffset  = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastX);
                            auto offset = shape::indexOffset(f, yShapeInfo, yShapeInfoCast, lenY, canCastY);
                            oZ[offset] = OpType::op(oX[xOffset], y[offset]);
                        }
                    }
                }
                else {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint tadShapeInfoZCast[MAX_RANK];
                    uint yShapeInfoCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(yShapeInfo, yShapeInfoCast);
                    bool canCastZ = nd4j::DataTypeUtils::castShapeInfo(zTadShapeInfo, tadShapeInfoZCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oX = x + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto xOffset  = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastX);
                            auto yOffset = shape::indexOffset(f, yShapeInfo, yShapeInfoCast, lenY, canCastY);
                            auto zOffset  = shape::indexOffset(f, zTadShapeInfo, tadShapeInfoZCast, lenZ, canCastZ);
                            oZ[zOffset] = OpType::op(oX[xOffset], y[yOffset]);
                        }
                    }
                }
        }


                template <typename X, typename Z>
        template<typename OpType>
        void BroadcastBool<X, Z>::execInverse(void *vx,
                             Nd4jLong *xShapeInfo,
                             void *vy,
                             Nd4jLong *yShapeInfo,
                             void *vz,
                             Nd4jLong *zShapeInfo,
                             int *dimension,
                             int dimensionLength,
                             Nd4jLong *xTadShapeInfo,
                             Nd4jLong *xTadOffset,
                             Nd4jLong *zTadShapeInfo,
                             Nd4jLong *zTadOffset) {

                auto x = reinterpret_cast<X *>(vx);
                auto y = reinterpret_cast<X *>(vy);
                auto z = reinterpret_cast<Z *>(vz);

                //decompose in to several sub tads after
                //moving all dimensions (in sorted order)
                //to the back.
                //permuted version of the x shape info for setting up the tad problem
                auto xTadShapeShapeInfo = xTadShapeInfo;
                auto tadOffsets = xTadOffset;

                if (xTadShapeInfo == nullptr || tadOffsets == nullptr) {
                    auto tadPack = nd4j::ConstantTadHelper::getInstance()->tadForDimensions(yShapeInfo, dimension, dimensionLength);

                    xTadShapeShapeInfo = tadPack.primaryShapeInfo();
                    tadOffsets = tadPack.primaryOffsets();
                }

                //int *resultStride = shape::stride(xTadShapeShapeInfo);
                unsigned int tadLength = shape::length(xTadShapeShapeInfo);
                unsigned int tads = shape::length(yShapeInfo) / tadLength;

                if (zTadShapeInfo == nullptr) {
                    zTadShapeInfo = xTadShapeShapeInfo;
                    zTadOffset = tadOffsets;
                }                                

                auto lenZ = shape::length(zTadShapeInfo);
                auto lenX = shape::length(xShapeInfo);

                int tadsPerThread = tads / TAD_THRESHOLD;
                int threads = nd4j::math::nd4j_max<int>(1, tadsPerThread);
                threads = nd4j::math::nd4j_min<int>(threads, omp_get_max_threads());

                auto yEws = shape::elementWiseStride(xTadShapeShapeInfo);
                auto xEws = shape::elementWiseStride(xShapeInfo);
                auto zEws = shape::elementWiseStride(zTadShapeInfo);

                if (shape::order(xTadShapeShapeInfo) == shape::order(xShapeInfo) && shape::order(zTadShapeInfo) == shape::order(xShapeInfo) && xEws > 0 && yEws > 0 && zEws > 0) {

                    if (xEws == 1 && yEws == 1 && zEws == 1) {
                        PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                        for (int i = 0; i < tads; i++) {
                            auto oY = y + tadOffsets[i];
                            auto oZ = z + zTadOffset[i];

                            PRAGMA_OMP_SIMD
                            for (unsigned int f = 0; f < tadLength; f++)
                                oZ[f] = OpType::op(x[f], oY[f]);
                        }
                    } else {
                        PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                        for (int i = 0; i < tads; i++) {
                            auto oY = y + tadOffsets[i];
                            auto oZ = z + zTadOffset[i];

                            PRAGMA_OMP_SIMD
                            for (uint f = 0; f < tadLength; f++)
                                oZ[f * zEws] = OpType::op(x[f * xEws], oY[f * yEws]);
                        }
                    }
                } else if(shape::haveSameOffsets(xTadShapeShapeInfo, xShapeInfo) && shape::haveSameOffsets(xTadShapeShapeInfo, zTadShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oY = y + tadOffsets[i];
                        auto oZ = z + zTadOffset[i];                        
                                        
                        // TODO: cover this codebranch with tests
                        // all this stuff already happens within thread
                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto offset = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastY);
                            oZ[offset] = OpType::op(x[offset], oY[offset]);
                        }
                    }
                }
                else if(shape::haveSameOffsets(xTadShapeShapeInfo, xShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint tadShapeInfoZCast[MAX_RANK];
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);
                    bool canCastZ = nd4j::DataTypeUtils::castShapeInfo(zTadShapeInfo, tadShapeInfoZCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oY = y + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto offset  = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastY);
                            auto zOffset = shape::indexOffset(f, zTadShapeInfo, tadShapeInfoZCast, lenZ, canCastZ);
                            oZ[zOffset] = OpType::op(x[offset], oY[offset]);
                        }
                    }
                }
                else if(shape::haveSameOffsets(xTadShapeShapeInfo, zTadShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint xShapeInfoCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xShapeInfo, xShapeInfoCast);
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);                    

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oY = y + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto offset  = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastY);
                            auto xOffset = shape::indexOffset(f, xShapeInfo, xShapeInfoCast, lenX, canCastX);
                            oZ[offset] = OpType::op(x[xOffset], oY[offset]);
                        }
                    }
                }
                else if(shape::haveSameOffsets(xShapeInfo, zTadShapeInfo)) {

                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint xShapeInfoCast[MAX_RANK];
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xShapeInfo, xShapeInfoCast);
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);
                    
                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oY = y + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto yOffset = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastY);
                            auto offset  = shape::indexOffset(f, xShapeInfo, xShapeInfoCast, lenX, canCastX);
                            oZ[offset] = OpType::op(x[offset], oY[yOffset]);
                        }
                    }
                }
                else {

                    uint xShapeInfoCast[MAX_RANK];
                    uint tadShapeShapeInfoCast[MAX_RANK];
                    uint tadShapeInfoZCast[MAX_RANK];                    
                    bool canCastX = nd4j::DataTypeUtils::castShapeInfo(xShapeInfo, xShapeInfoCast);
                    bool canCastY = nd4j::DataTypeUtils::castShapeInfo(xTadShapeShapeInfo, tadShapeShapeInfoCast);                
                    bool canCastZ = nd4j::DataTypeUtils::castShapeInfo(zTadShapeInfo, tadShapeInfoZCast);

                    PRAGMA_OMP_PARALLEL_FOR_THREADS(threads)
                    for (int i = 0; i < tads; i++) {
                    
                        auto oZ = z + zTadOffset[i];
                        auto oY = y + tadOffsets[i];

                        PRAGMA_OMP_SIMD
                        for (int f = 0; f < tadLength; f++) {
                            auto xOffset = shape::indexOffset(f, xShapeInfo, xShapeInfoCast, lenX, canCastX);
                            auto yOffset = shape::indexOffset(f, xTadShapeShapeInfo, tadShapeShapeInfoCast, tadLength, canCastY);                            
                            auto zOffset = shape::indexOffset(f, zTadShapeInfo, tadShapeInfoZCast, lenZ, canCastZ);
                            oZ[zOffset] = OpType::op(x[xOffset], oY[yOffset]);
                        }
                    }
                }
        }

        BUILD_DOUBLE_TEMPLATE(template class ND4J_EXPORT BroadcastBool, , LIBND4J_TYPES, BOOL_TYPES);
    }
}