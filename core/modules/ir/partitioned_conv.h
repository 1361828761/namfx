#pragma once

#include "modules/ir/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace namfx {
namespace ir {

// Uniform-partitioned overlap-save convolution (Gardner 1995). The impulse
// response is split into P blocks of M samples; each block's spectrum is
// precomputed. Every M input samples one FFT of the 2M overlap-save segment
// is taken, multiplied with each partition kernel and inverse-transformed;
// the valid halves accumulate into a preallocated ring (no allocation in
// process). Effective latency is one block. Double precision to hold the
// < -100 dB contract against the direct-convolution reference.
class PartitionedConvolver {
public:
    void prepare(const std::vector<float>& h, int blockSize)
    {
        block_ = std::max(blockSize, 16);
        fftLen_ = 2 * block_;
        parts_ = (static_cast<int>(h.size()) + block_ - 1) / block_;
        if (parts_ < 1) {
            parts_ = 1;
        }

        hFreq_.assign(static_cast<std::size_t>(parts_),
                      std::vector<std::complex<double>>(static_cast<std::size_t>(fftLen_)));
        std::vector<std::complex<double>> tmp(static_cast<std::size_t>(fftLen_));
        for (int p = 0; p < parts_; ++p) {
            std::fill(tmp.begin(), tmp.end(), std::complex<double>(0.0, 0.0));
            const int begin = p * block_;
            const int end = std::min(begin + block_, static_cast<int>(h.size()));
            for (int i = begin; i < end; ++i) {
                tmp[static_cast<std::size_t>(i - begin)] = h[static_cast<std::size_t>(i)];
            }
            fftInPlace(tmp, false);
            hFreq_[static_cast<std::size_t>(p)] = tmp;
        }

        inSeg_.assign(static_cast<std::size_t>(fftLen_), 0.0f);
        outBuf_.assign(static_cast<std::size_t>((parts_ + 2) * block_), 0.0f);
        fftBuf_.assign(static_cast<std::size_t>(fftLen_), std::complex<double>(0.0, 0.0));
        segOut_.assign(static_cast<std::size_t>(block_), 0.0f);
        inCount_ = 0;
        blockIndex_ = 0;
        segPos_ = block_;
    }

    void reset()
    {
        std::fill(inSeg_.begin(), inSeg_.end(), 0.0f);
        std::fill(outBuf_.begin(), outBuf_.end(), 0.0f);
        std::fill(segOut_.begin(), segOut_.end(), 0.0f);
        inCount_ = 0;
        blockIndex_ = 0;
        segPos_ = block_;
    }

    // stream n input samples; write n output samples (zero during warm-up)
    void process(const float* in, int n, float* out)
    {
        for (int i = 0; i < n; ++i) {
            inSeg_[static_cast<std::size_t>(block_ + inCount_)] = in[i];
            ++inCount_;
            if (inCount_ == block_) {
                processBlock();
            }
            out[i] = segPos_ < block_ ? segOut_[static_cast<std::size_t>(segPos_++)] : 0.0f;
        }
    }

private:
    void processBlock()
    {
        // forward FFT of the 2M overlap-save segment
        for (int i = 0; i < fftLen_; ++i) {
            fftBuf_[static_cast<std::size_t>(i)] = inSeg_[static_cast<std::size_t>(i)];
        }
        fftInPlace(fftBuf_, false);

        // multiply with each partition kernel and accumulate the valid half
        // into the ring at offset p*M from this block's output position
        for (int p = 0; p < parts_; ++p) {
            for (int i = 0; i < fftLen_; ++i) {
                fftBuf_[static_cast<std::size_t>(i)] *= hFreq_[static_cast<std::size_t>(p)]
                                                              [static_cast<std::size_t>(i)];
            }
            fftInPlace(fftBuf_, true);
            const long long base = (blockIndex_ + p) * static_cast<long long>(block_);
            const std::size_t ring = outBuf_.size();
            for (int m = 0; m < block_; ++m) {
                const std::size_t pos = static_cast<std::size_t>((base + m) % static_cast<long long>(ring));
                outBuf_[pos] += static_cast<float>(fftBuf_[static_cast<std::size_t>(m + block_)].real());
            }
            // restore fftBuf for the next partition multiply
            for (int i = 0; i < fftLen_; ++i) {
                fftBuf_[static_cast<std::size_t>(i)] = inSeg_[static_cast<std::size_t>(i)];
            }
            fftInPlace(fftBuf_, false);
        }

        // emit this block's output segment (complete once block k is in)
        const long long base = blockIndex_ * static_cast<long long>(block_);
        const std::size_t ring = outBuf_.size();
        for (int m = 0; m < block_; ++m) {
            const std::size_t pos = static_cast<std::size_t>((base + m) % static_cast<long long>(ring));
            segOut_[static_cast<std::size_t>(m)] = outBuf_[pos];
            outBuf_[pos] = 0.0f;
        }
        segPos_ = 0;

        // slide the overlap-save segment
        std::copy(inSeg_.begin() + block_, inSeg_.begin() + fftLen_, inSeg_.begin());
        inCount_ = 0;
        ++blockIndex_;
    }

    int block_ = 1024;
    int fftLen_ = 2048;
    int parts_ = 1;

    std::vector<std::vector<std::complex<double>>> hFreq_;
    std::vector<float> inSeg_;
    std::vector<float> outBuf_;
    std::vector<std::complex<double>> fftBuf_;
    std::vector<float> segOut_;
    int inCount_ = 0;
    long long blockIndex_ = 0;
    int segPos_ = 0;
};

} // namespace ir
} // namespace namfx
