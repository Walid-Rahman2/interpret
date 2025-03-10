// Copyright (c) 2018 Microsoft Corporation
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "precompiled_header_cpp.hpp"

#include <stddef.h> // size_t, ptrdiff_t

#include "logging.h" // EBM_ASSERT
#include "zones.h"

#include "ebm_internal.hpp" // k_cCompilerClassesMax

#include "Term.hpp"
#include "InnerBag.hpp"
#include "GradientPair.hpp"
#include "Bin.hpp"
#include "BoosterCore.hpp"
#include "BoosterShell.hpp"

namespace DEFINED_ZONE_NAME {
#ifndef DEFINED_ZONE_NAME
#error DEFINED_ZONE_NAME must be defined
#endif // DEFINED_ZONE_NAME

template<ptrdiff_t cCompilerClasses>
class BinSumsBoostingZeroDimensions final {
public:

   BinSumsBoostingZeroDimensions() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const InnerBag * const pInnerBag
   ) {
      static constexpr bool bClassification = IsClassification(cCompilerClasses);
      static constexpr size_t cCompilerScores = GetCountScores(cCompilerClasses);

      LOG_0(Trace_Verbose, "Entered BinSumsBoostingZeroDimensions");

      auto * const pBin = pBoosterShell->GetBoostingFastBinsTemp()->Specialize<FloatFast, bClassification, cCompilerScores>();

      BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
      const ptrdiff_t cRuntimeClasses = pBoosterCore->GetCountClasses();

      const ptrdiff_t cClasses = GET_COUNT_CLASSES(cCompilerClasses, cRuntimeClasses);
      const size_t cScores = GetCountScores(cClasses);

      const size_t cSamples = pBoosterCore->GetTrainingSet()->GetCountSamples();
      EBM_ASSERT(0 < cSamples);

      const size_t * pCountOccurrences = pInnerBag->GetCountOccurrences();
      const FloatFast * pWeight = pInnerBag->GetWeights();
      EBM_ASSERT(nullptr != pWeight);
#ifndef NDEBUG
      FloatFast weightTotalDebug = 0;
#endif // NDEBUG

      const FloatFast * pGradientAndHessian = pBoosterCore->GetTrainingSet()->GetGradientsAndHessiansPointer();
      // this shouldn't overflow since we're accessing existing memory
      const FloatFast * const pGradientAndHessiansEnd = pGradientAndHessian + (bClassification ? 2 : 1) * cScores * cSamples;

      auto * const pGradientPair = pBin->GetGradientPairs();
      do {
         // this loop gets about twice as slow if you add a single unpredictable branching if statement based on count, even if you still access all the memory
         //   in complete sequential order, so we'll probably want to use non-branching instructions for any solution like conditional selection or multiplication
         // this loop gets about 3 times slower if you use a bad pseudo random number generator like rand(), although it might be better if you inlined rand().
         // this loop gets about 10 times slower if you use a proper pseudo random number generator like std::default_random_engine
         // taking all the above together, it seems unlikley we'll use a method of separating sets via single pass randomized set splitting.  Even if count is 
         //   stored in memory if shouldn't increase the time spent fetching it by 2 times, unless our bottleneck when threading is overwhelmingly memory 
         //   pressure related, and even then we could store the count for a single bit aleviating the memory pressure greatly, if we use the right 
         //   sampling method 

         // TODO : try using a sampling method with non-repeating samples, and put the count into a bit.  Then unwind that loop either at the byte level 
         //   (8 times) or the uint64_t level.  This can be done without branching and doesn't require random number generators

         const size_t cOccurences = *pCountOccurrences;
         const FloatFast weight = *pWeight;

#ifndef NDEBUG
         weightTotalDebug += weight;
#endif // NDEBUG

         ++pCountOccurrences;
         ++pWeight;
         pBin->SetCountSamples(pBin->GetCountSamples() + cOccurences);
         pBin->SetWeight(pBin->GetWeight() + weight);

         size_t iScore = 0;

#ifndef NDEBUG
#ifdef EXPAND_BINARY_LOGITS
         static constexpr bool bExpandBinaryLogits = true;
#else // EXPAND_BINARY_LOGITS
         static constexpr bool bExpandBinaryLogits = false;
#endif // EXPAND_BINARY_LOGITS
         FloatFast sumGradientsDebug = 0;
#endif // NDEBUG
         do {
            const FloatFast gradient = *pGradientAndHessian;
#ifndef NDEBUG
            sumGradientsDebug += gradient;
#endif // NDEBUG
            pGradientPair[iScore].m_sumGradients += gradient * weight;
            if(bClassification) {
               // TODO : this code gets executed for each InnerBag set.  I could probably execute it once and then all the 
               //   InnerBag sets would have this value, but I would need to store the computation in a new memory place, and it might make 
               //   more sense to calculate this values in the CPU rather than put more pressure on memory.  I think controlling this should be done in a 
               //   MACRO and we should use a class to hold the gradient and this computation from that value and then comment out the computation if 
               //   not necssary and access it through an accessor so that we can make the change entirely via macro
               const FloatFast hessian = *(pGradientAndHessian + 1);
               pGradientPair[iScore].SetHess(pGradientPair[iScore].GetHess() + hessian * weight);
            }
            pGradientAndHessian += bClassification ? 2 : 1;
            ++iScore;
            // if we use this specific format where (iScore < cScores) then the compiler collapses alway the loop for small cScores values
            // if we make this (iScore != cScores) then the loop is not collapsed
            // the compiler seems to not mind if we make this a for loop or do loop in terms of collapsing away the loop
         } while(iScore < cScores);

         EBM_ASSERT(
            !bClassification ||
            ptrdiff_t { 2 } == cRuntimeClasses && !bExpandBinaryLogits ||
            std::isnan(sumGradientsDebug) ||
            -k_epsilonGradient < sumGradientsDebug && sumGradientsDebug < k_epsilonGradient
         );
      } while(pGradientAndHessiansEnd != pGradientAndHessian);
      
      EBM_ASSERT(0 < weightTotalDebug);
      EBM_ASSERT(static_cast<FloatBig>(weightTotalDebug * 0.999) <= pInnerBag->GetWeightTotal() &&
         pInnerBag->GetWeightTotal() <= static_cast<FloatBig>(1.001 * weightTotalDebug));

      LOG_0(Trace_Verbose, "Exited BinSumsBoostingZeroDimensions");
   }
};

template<ptrdiff_t cPossibleClasses>
class BinSumsBoostingZeroDimensionsTarget final {
public:

   BinSumsBoostingZeroDimensionsTarget() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const InnerBag * const pInnerBag
   ) {
      static_assert(IsClassification(cPossibleClasses), "cPossibleClasses needs to be a classification");
      static_assert(cPossibleClasses <= k_cCompilerClassesMax, "We can't have this many items in a data pack.");

      BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
      const ptrdiff_t cRuntimeClasses = pBoosterCore->GetCountClasses();
      EBM_ASSERT(IsClassification(cRuntimeClasses));
      EBM_ASSERT(cRuntimeClasses <= k_cCompilerClassesMax);

      if(cPossibleClasses == cRuntimeClasses) {
         BinSumsBoostingZeroDimensions<cPossibleClasses>::Func(
            pBoosterShell,
            pInnerBag
         );
      } else {
         BinSumsBoostingZeroDimensionsTarget<cPossibleClasses + 1>::Func(
            pBoosterShell,
            pInnerBag
         );
      }
   }
};

template<>
class BinSumsBoostingZeroDimensionsTarget<k_cCompilerClassesMax + 1> final {
public:

   BinSumsBoostingZeroDimensionsTarget() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const InnerBag * const pInnerBag
   ) {
      static_assert(IsClassification(k_cCompilerClassesMax), "k_cCompilerClassesMax needs to be a classification");

      EBM_ASSERT(IsClassification(pBoosterShell->GetBoosterCore()->GetCountClasses()));
      EBM_ASSERT(k_cCompilerClassesMax < pBoosterShell->GetBoosterCore()->GetCountClasses());

      BinSumsBoostingZeroDimensions<k_dynamicClassification>::Func(
         pBoosterShell,
         pInnerBag
      );
   }
};

template<ptrdiff_t cCompilerClasses, ptrdiff_t compilerBitPack>
class BinSumsBoostingInternal final {
public:

   BinSumsBoostingInternal() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      // TODO: work to do here:
      //        1) merge this function with BinSumsBoostingZeroDimensions above following the style in ApplyTermUpdateValidation
      //        2) finish writing BinSumsInteractionsInternal to handle bit compressed data which should look similar to this function
      //        3) merge this function into ApplyTermUpdateValidation so that you can optionally avoid creating the per sample gradients and hessians and 
      //           boost on the next feature while simultaneously applying the previous one.  This could allow us to
      //           avoid extra memory accesses and also bury long running computations like random access fetches
      //           and divisions into the CPU pipeline

      static constexpr bool bClassification = IsClassification(cCompilerClasses);
      static constexpr size_t cCompilerScores = GetCountScores(cCompilerClasses);

      LOG_0(Trace_Verbose, "Entered BinSumsBoostingInternal");

      auto * const aBins = pBoosterShell->GetBoostingFastBinsTemp()->Specialize<FloatFast, bClassification, cCompilerScores>();

      BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
      const ptrdiff_t cRuntimeClasses = pBoosterCore->GetCountClasses();

      const ptrdiff_t cClasses = GET_COUNT_CLASSES(cCompilerClasses, cRuntimeClasses);
      const size_t cScores = GetCountScores(cClasses);

      EBM_ASSERT(iTerm < pBoosterCore->GetCountTerms());
      const Term * const pTerm = pBoosterCore->GetTerms()[iTerm];

      const size_t cItemsPerBitPack = static_cast<size_t>(GET_ITEMS_PER_BIT_PACK(compilerBitPack, pTerm->GetBitPack()));
      EBM_ASSERT(size_t { 1 } <= cItemsPerBitPack);
      EBM_ASSERT(cItemsPerBitPack <= k_cBitsForStorageType);
      const size_t cBitsPerItemMax = GetCountBits(cItemsPerBitPack);
      EBM_ASSERT(1 <= cBitsPerItemMax);
      EBM_ASSERT(cBitsPerItemMax <= k_cBitsForStorageType);
      const size_t maskBits = (~size_t { 0 }) >> (k_cBitsForSizeT - cBitsPerItemMax);
      EBM_ASSERT(!IsOverflowBinSize<FloatFast>(bClassification, cScores)); // we're accessing allocated memory
      const size_t cBytesPerBin = GetBinSize<FloatFast>(bClassification, cScores);

      const size_t cSamples = pBoosterCore->GetTrainingSet()->GetCountSamples();
      EBM_ASSERT(0 < cSamples);

      const size_t * pCountOccurrences = pInnerBag->GetCountOccurrences();
      const FloatFast * pWeight = pInnerBag->GetWeights();
      EBM_ASSERT(nullptr != pWeight);
#ifndef NDEBUG
      FloatFast weightTotalDebug = 0;
#endif // NDEBUG

      const StorageDataType * pInputData = pBoosterCore->GetTrainingSet()->GetInputDataPointer(iTerm);
      const FloatFast * pGradientAndHessian = pBoosterCore->GetTrainingSet()->GetGradientsAndHessiansPointer();

      // this shouldn't overflow since we're accessing existing memory
      const FloatFast * const pGradientAndHessiansEnd = pGradientAndHessian + (bClassification ? 2 : 1) * cScores * cSamples;

      ptrdiff_t cShift = (cSamples - 1) % cItemsPerBitPack * cBitsPerItemMax;
      const ptrdiff_t cShiftReset = cBitsPerItemMax * (cItemsPerBitPack - 1);

      do {
         // this loop gets about twice as slow if you add a single unpredictable branching if statement based on count, even if you still access all the memory
         // in complete sequential order, so we'll probably want to use non-branching instructions for any solution like conditional selection or multiplication
         // this loop gets about 3 times slower if you use a bad pseudo random number generator like rand(), although it might be better if you inlined rand().
         // this loop gets about 10 times slower if you use a proper pseudo random number generator like std::default_random_engine
         // taking all the above together, it seems unlikley we'll use a method of separating sets via single pass randomized set splitting.  Even if count is 
         // stored in memory if shouldn't increase the time spent fetching it by 2 times, unless our bottleneck when threading is overwhelmingly memory pressure
         // related, and even then we could store the count for a single bit aleviating the memory pressure greatly, if we use the right sampling method 

         // TODO : try using a sampling method with non-repeating samples, and put the count into a bit.  Then unwind that loop either at the byte level 
         //   (8 times) or the uint64_t level.  This can be done without branching and doesn't require random number generators

         // we store the already multiplied dimensional value in *pInputData
         const StorageDataType iTensorBinCombined = *pInputData;
         ++pInputData;
         do {
            // TODO: we should assert at least that we can convert to size_t after the shift

            const size_t iTensorBin = static_cast<size_t>(iTensorBinCombined >> cShift) & maskBits;

            auto * const pBin = IndexBin(aBins, cBytesPerBin * iTensorBin);

            ASSERT_BIN_OK(cBytesPerBin, pBin, pBoosterShell->GetDebugFastBinsEnd());
            const size_t cOccurences = *pCountOccurrences;
            const FloatFast weight = *pWeight;

#ifndef NDEBUG
            weightTotalDebug += weight;
#endif // NDEBUG

            ++pCountOccurrences;
            ++pWeight;
            pBin->SetCountSamples(pBin->GetCountSamples() + cOccurences);
            pBin->SetWeight(pBin->GetWeight() + weight);

            auto * pGradientPair = pBin->GetGradientPairs();

            size_t iScore = 0;

#ifndef NDEBUG
#ifdef EXPAND_BINARY_LOGITS
            static constexpr bool bExpandBinaryLogits = true;
#else // EXPAND_BINARY_LOGITS
            static constexpr bool bExpandBinaryLogits = false;
#endif // EXPAND_BINARY_LOGITS
            FloatFast gradientTotalDebug = 0;
#endif // NDEBUG
            do {
               const FloatFast gradient = *pGradientAndHessian;
#ifndef NDEBUG
               gradientTotalDebug += gradient;
#endif // NDEBUG
               pGradientPair[iScore].m_sumGradients += gradient * weight;
               if(bClassification) {
                  // TODO : this code gets executed for each InnerBag set.  I could probably execute it once and then all the
                  //   InnerBag sets would have this value, but I would need to store the computation in a new memory place, and it might 
                  //   make more sense to calculate this values in the CPU rather than put more pressure on memory.  I think controlling this should be 
                  //   done in a MACRO and we should use a class to hold the gradient and this computation from that value and then comment out the 
                  //   computation if not necssary and access it through an accessor so that we can make the change entirely via macro
                  const FloatFast hessian = *(pGradientAndHessian + 1);
                  pGradientPair[iScore].SetHess(pGradientPair[iScore].GetHess() + hessian * weight);
               }
               pGradientAndHessian += bClassification ? 2 : 1;
               ++iScore;
               // if we use this specific format where (iScore < cScores) then the compiler collapses alway the loop for small cScores values
               // if we make this (iScore != cScores) then the loop is not collapsed
               // the compiler seems to not mind if we make this a for loop or do loop in terms of collapsing away the loop
            } while(iScore < cScores);

            EBM_ASSERT(
               !bClassification ||
               ptrdiff_t { 2 } == cRuntimeClasses && !bExpandBinaryLogits ||
               -k_epsilonGradient < gradientTotalDebug && gradientTotalDebug < k_epsilonGradient
            );

            cShift -= cBitsPerItemMax;
         } while(0 <= cShift);
         cShift = cShiftReset;
      } while(pGradientAndHessiansEnd != pGradientAndHessian);

      EBM_ASSERT(0 < weightTotalDebug);
      EBM_ASSERT(static_cast<FloatBig>(weightTotalDebug * 0.999) <= pInnerBag->GetWeightTotal() &&
         pInnerBag->GetWeightTotal() <= static_cast<FloatBig>(1.001 * weightTotalDebug));

      LOG_0(Trace_Verbose, "Exited BinSumsBoostingInternal");
   }
};

template<ptrdiff_t cPossibleClasses>
class BinSumsBoostingNormalTarget final {
public:

   BinSumsBoostingNormalTarget() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      static_assert(IsClassification(cPossibleClasses), "cPossibleClasses needs to be a classification");
      static_assert(cPossibleClasses <= k_cCompilerClassesMax, "We can't have this many items in a data pack.");

      BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
      const ptrdiff_t cRuntimeClasses = pBoosterCore->GetCountClasses();
      EBM_ASSERT(IsClassification(cRuntimeClasses));
      EBM_ASSERT(cRuntimeClasses <= k_cCompilerClassesMax);

      if(cPossibleClasses == cRuntimeClasses) {
         BinSumsBoostingInternal<cPossibleClasses, k_cItemsPerBitPackDynamic>::Func(
            pBoosterShell,
            iTerm,
            pInnerBag
         );
      } else {
         BinSumsBoostingNormalTarget<cPossibleClasses + 1>::Func(
            pBoosterShell,
            iTerm,
            pInnerBag
         );
      }
   }
};

template<>
class BinSumsBoostingNormalTarget<k_cCompilerClassesMax + 1> final {
public:

   BinSumsBoostingNormalTarget() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      static_assert(IsClassification(k_cCompilerClassesMax), "k_cCompilerClassesMax needs to be a classification");

      EBM_ASSERT(IsClassification(pBoosterShell->GetBoosterCore()->GetCountClasses()));
      EBM_ASSERT(k_cCompilerClassesMax < pBoosterShell->GetBoosterCore()->GetCountClasses());

      BinSumsBoostingInternal<k_dynamicClassification, k_cItemsPerBitPackDynamic>::Func(
         pBoosterShell,
         iTerm,
         pInnerBag
      );
   }
};

template<ptrdiff_t cCompilerClasses, ptrdiff_t compilerBitPack>
class BinSumsBoostingSIMDPacking final {
public:

   BinSumsBoostingSIMDPacking() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
      EBM_ASSERT(iTerm < pBoosterCore->GetCountTerms());
      const Term * const pTerm = pBoosterCore->GetTerms()[iTerm];

      const ptrdiff_t runtimeBitPack = pTerm->GetBitPack();

      EBM_ASSERT(ptrdiff_t { 1 } <= runtimeBitPack);
      EBM_ASSERT(runtimeBitPack <= ptrdiff_t { k_cBitsForStorageType });
      static_assert(compilerBitPack <= ptrdiff_t { k_cBitsForStorageType }, "We can't have this many items in a data pack.");
      if(compilerBitPack == runtimeBitPack) {
         BinSumsBoostingInternal<cCompilerClasses, compilerBitPack>::Func(
            pBoosterShell,
            iTerm,
            pInnerBag
         );
      } else {
         BinSumsBoostingSIMDPacking<
            cCompilerClasses,
            GetNextCountItemsBitPacked(compilerBitPack)
         >::Func(
            pBoosterShell,
            iTerm,
            pInnerBag
         );
      }
   }
};

template<ptrdiff_t cCompilerClasses>
class BinSumsBoostingSIMDPacking<cCompilerClasses, k_cItemsPerBitPackDynamic> final {
public:

   BinSumsBoostingSIMDPacking() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      EBM_ASSERT(iTerm < pBoosterShell->GetBoosterCore()->GetCountTerms());
      EBM_ASSERT(ptrdiff_t { 1 } <= pBoosterShell->GetBoosterCore()->GetTerms()[iTerm]->GetBitPack());
      EBM_ASSERT(pBoosterShell->GetBoosterCore()->GetTerms()[iTerm]->GetBitPack() <= ptrdiff_t { k_cBitsForStorageType });
      BinSumsBoostingInternal<cCompilerClasses, k_cItemsPerBitPackDynamic>::Func(
         pBoosterShell,
         iTerm,
         pInnerBag
      );
   }
};

template<ptrdiff_t cPossibleClasses>
class BinSumsBoostingSIMDTarget final {
public:

   BinSumsBoostingSIMDTarget() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      static_assert(IsClassification(cPossibleClasses), "cPossibleClasses needs to be a classification");
      static_assert(cPossibleClasses <= k_cCompilerClassesMax, "We can't have this many items in a data pack.");

      BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
      const ptrdiff_t cRuntimeClasses = pBoosterCore->GetCountClasses();
      EBM_ASSERT(IsClassification(cRuntimeClasses));
      EBM_ASSERT(cRuntimeClasses <= k_cCompilerClassesMax);

      if(cPossibleClasses == cRuntimeClasses) {
         BinSumsBoostingSIMDPacking<
            cPossibleClasses,
            k_cItemsPerBitPackMax
         >::Func(
            pBoosterShell,
            iTerm,
            pInnerBag
         );
      } else {
         BinSumsBoostingSIMDTarget<cPossibleClasses + 1>::Func(
            pBoosterShell,
            iTerm,
            pInnerBag
         );
      }
   }
};

template<>
class BinSumsBoostingSIMDTarget<k_cCompilerClassesMax + 1> final {
public:

   BinSumsBoostingSIMDTarget() = delete; // this is a static class.  Do not construct

   INLINE_RELEASE_UNTEMPLATED static void Func(
      BoosterShell * const pBoosterShell,
      const size_t iTerm,
      const InnerBag * const pInnerBag
   ) {
      static_assert(IsClassification(k_cCompilerClassesMax), "k_cCompilerClassesMax needs to be a classification");

      EBM_ASSERT(IsClassification(pBoosterShell->GetBoosterCore()->GetCountClasses()));
      EBM_ASSERT(k_cCompilerClassesMax < pBoosterShell->GetBoosterCore()->GetCountClasses());

      BinSumsBoostingSIMDPacking<k_dynamicClassification, k_cItemsPerBitPackMax>::Func(
         pBoosterShell,
         iTerm,
         pInnerBag
      );
   }
};

extern void BinSumsBoosting(
   BoosterShell * const pBoosterShell,
   const size_t iTerm,
   const InnerBag * const pInnerBag
) {
   LOG_0(Trace_Verbose, "Entered BinSumsBoosting");

   BoosterCore * const pBoosterCore = pBoosterShell->GetBoosterCore();
   const ptrdiff_t cRuntimeClasses = pBoosterCore->GetCountClasses();

   if(BoosterShell::k_illegalTermIndex == iTerm) {
      if(IsClassification(cRuntimeClasses)) {
         BinSumsBoostingZeroDimensionsTarget<2>::Func(
            pBoosterShell,
            pInnerBag
         );
      } else {
         EBM_ASSERT(IsRegression(cRuntimeClasses));
         BinSumsBoostingZeroDimensions<k_regression>::Func(
            pBoosterShell,
            pInnerBag
         );
      }
   } else {
      EBM_ASSERT(iTerm < pBoosterCore->GetCountTerms());
      EBM_ASSERT(1 <= pBoosterCore->GetTerms()[iTerm]->GetCountRealDimensions());
      constexpr bool k_bTemplateBitPacking = false; // TODO: make this work in the future
      if(k_bTemplateBitPacking) {
         if(IsClassification(cRuntimeClasses)) {
            BinSumsBoostingSIMDTarget<2>::Func(
               pBoosterShell,
               iTerm,
               pInnerBag
            );
         } else {
            EBM_ASSERT(IsRegression(cRuntimeClasses));
            BinSumsBoostingSIMDPacking<k_regression, k_cItemsPerBitPackMax>::Func(
               pBoosterShell,
               iTerm,
               pInnerBag
            );
         }
      } else {
         if(IsClassification(cRuntimeClasses)) {
            BinSumsBoostingNormalTarget<2>::Func(
               pBoosterShell,
               iTerm,
               pInnerBag
            );
         } else {
            EBM_ASSERT(IsRegression(cRuntimeClasses));
            BinSumsBoostingInternal<k_regression, k_cItemsPerBitPackDynamic>::Func(
               pBoosterShell,
               iTerm,
               pInnerBag
            );
         }
      }
   }

   LOG_0(Trace_Verbose, "Exited BinSumsBoosting");
}

} // DEFINED_ZONE_NAME
