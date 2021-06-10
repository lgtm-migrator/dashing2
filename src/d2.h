#ifndef D2_H_H___
#define D2_H_H___
#ifdef _OPENMP
#include <omp.h>
#endif
#include "enums.h"
#include <memory>
#include <vector>
#include "bonsai/encoder.h"
#include "flat_hash_map/flat_hash_map.hpp"
#include "xxHash/xxh3.h"
#include "sketch/setsketch.h"
#include "sketch/bmh.h"
#include "counter.h"
#include "oph.h"
#include "filterset.h"


namespace dashing2 {
using namespace sketch;

// To allow for 64-bit set identifiers, compile with -DLSHIDTYPE=uint64_t
#ifndef LSHIDTYPE
#define LSHIDTYPE uint32_t
#endif
using LSHIDType = LSHIDTYPE;
#undef LSHIDTYPE

#ifndef DASHING2_INDEX_FLOAT_TYPE
#define DASHING2_INDEX_FLOAT_TYPE float
#endif
using LSHDistType = DASHING2_INDEX_FLOAT_TYPE;
#undef DASHING2_INDEX_FLOAT_TYPE


struct IntervalSketchResult {
    using Map = ska::flat_hash_map<std::string, std::vector<RegT>>;
    std::unique_ptr<Map> chrmap_;
    std::unique_ptr<std::vector<RegT>> global_;
    double card_;
};

template<typename F>
void for_each_substr(const F &func, const std::string &s, const int sep=' ') {
    const char *p;
    if((p = std::strchr(s.data(), sep)) == nullptr) {
        func(s);
        return;
    }
    const char *p2 = s.data();
    std::string tmp(p2, p);
    for(;;) {
        func(tmp);
        std::swap(p2, ++p);
        if((p = std::strchr(p2, sep)) == nullptr) {
            tmp = p2;
            func(tmp);
            break;
        }
        tmp = std::string(p2, p);
        if(std::all_of(tmp.begin(), tmp.end(), [](auto x) {return std::isspace(x);})) break;
    }
}

struct Dashing2Options {

    // K-mer options
    int k_, w_;
    bns::Spacer sp_;
    bns::Encoder<> enc_;
    bns::RollingHasher<uint64_t> rh_;
    bns::RollingHasher<u128_t> rh128_;
    bns::RollingHashingType rht_;
    bool parse_by_seq_ = false;
    bool trim_chr_ = true;
    size_t sketchsize_ = 2048;
    double count_threshold_ = 0.;
    KmerSketchResultType kmer_result_ = ONE_PERM;
    bool by_chrom_ = false;
    bool bed_parse_normalize_intervals_ = false;
    size_t cssize_ = 0;
    bool save_kmers_ = false;
    bool save_kmercounts_ = false;
    bool homopolymer_compress_minimizers_ = false;
    bool trim_folder_paths_ = false; // When writing output files, write to cwd instead of the directory the files came from
    bool cache_sketches_ = false;
    bool build_mmer_matrix_ = false;
    bool build_count_matrix_ = false;
    bool build_sig_matrix_ = true;
    std::string outprefix_;
    std::string spacing_;
    std::string cmd_;

    // Whether to sketch multiset, set, or discrete probability distributions

    SketchSpace sspace_;
    DataType dtype_;
    bool use128_ = false;
    unsigned nthreads_;

    std::unique_ptr<FilterSet> fs_;
    Dashing2Options(int k, int w=-1, bns::RollingHashingType rht=bns::DNA, SketchSpace space=SPACE_SET, DataType dtype=FASTX, size_t nt=0, bool use128=false, std::string spacing=""):
        k_(k), w_(w), sp_(k, w > 0 ? w: k, spacing.data()), enc_(sp_), rh_(k), rh128_(k), rht_(rht), spacing_(spacing), sspace_(space), dtype_(dtype), use128_(use128) {
        std::fprintf(stderr, "Dashing2 made with k = %d, w = %d, space = %s, datatype = %s\n", k, w, ::dashing2::to_string(sspace_).data(), ::dashing2::to_string(dtype_).data());
        if(nt <= 0) {
            DBG_ONLY(std::fprintf(stderr, "[%s:%s:%d] num threads < 0, checking OMP_NUM_THREADS\n", __FILE__, __func__, __LINE__);)
            if(char *s = std::getenv("OMP_NUM_THREADS"))
                nt = std::max(std::atoi(s), 1);
            else nt = 1;
        }
        nthreads(nt);
    }
    void w(int neww) {w_ = neww; sp_.resize(k_, w_); rh128_.window(neww); rh_.window(neww);}
    std::string to_string() const;
    void validate const ();
#define D2O(name, oname)\
    Dashing2Options &oname(decltype(name) x) {name = x; return *this;}\
    std::add_const_t<decltype(name)> &oname() const {return name;}\
    decltype(name) &oname() {return name;}
#define D2O2(name) D2O(name##_, name)
    D2O2(cmd) D2O2(outprefix) D2O2(save_kmers)
    D2O2(save_kmercounts) D2O2(homopolymer_compress_minimizers)
    D2O2(kmer_result) D2O2(use128) D2O2(cache_sketches)
    D2O2(sketchsize) D2O2(nthreads) D2O2(cssize) D2O2(parse_by_seq)
#undef D2O
#undef D2O2
    // Getters and setters for all of the above
    Dashing2Options &parse_bigwig() {dtype_ = BIGWIG; return *this;}
    Dashing2Options &parse_bed() {dtype_ = BED; return *this;}
    Dashing2Options &parse_protein() {rh_.enctype_ = rh128_.enctype_ = rht_ = bns::PROTEIN; return *this;}
    CountingType ct() const {return cssize_ > 0 ? COUNTSKETCH_COUNTING: EXACT_COUNTING;}
    CountingType count() const {return ct();}
    bool trim_folder_paths() const {
        return trim_folder_paths_ || outprefix_.size();
    }
    bool canonicalize() const {return enc_.canonicalize();}
    void canonicalize(bool val) {
        enc_.canonicalize(val);
    }
    auto w() const {return w_;}
    bool one_perm() const {return kmer_result_ == ONE_PERM && sspace_ == SPACE_SET;}
    double count_threshold() const {return count_threshold_;}
};




using KmerSigT = std::conditional_t<(sizeof(RegT) == 8), uint64_t, std::conditional_t<(sizeof(RegT) == 4), uint32_t, u128_t>>;
using FullSetSketch = sketch::CSetSketch<RegT>;
using OPSetSketch = LazyOnePermSetSketch<KmerSigT>;
using BagMinHash = sketch::BagMinHash2<RegT>;
using ProbMinHash = sketch::pmh2_t<RegT>;
using OrderMinHash = sketch::omh::OMHasher<RegT>;

} // namespace dashing2
//std::vector<RegT> reduce(ska::flat_hash_map<std::string, std::vector<RegT>> &map);

#endif