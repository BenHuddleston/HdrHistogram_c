/**
 * hdr_histogram.h
 * Written by Michael Barker and released to the public domain,
 * as explained at http://creativecommons.org/publicdomain/zero/1.0/
 *
 * The source for the hdr_histogram utilises a few C99 constructs, specifically
 * the use of stdint/stdbool and inline variable declaration.
 */

#ifndef HDR_HISTOGRAM_H
#define HDR_HISTOGRAM_H 1

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * Macros below provide atomic interop between C++ and C as
 * <stdatomic.h> is not compatible with C++ see
 * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0943r1.html for
 * more information.
 */
#ifdef __cplusplus /* for std C++11 compiler */
    #include <atomic>
    #define _Atomic(T) std::atomic<T>

    using std::memory_order;
    using std::memory_order_relaxed;
    using std::memory_order_consume;
    using std::memory_order_acquire;
    using std::memory_order_release;
    using std::memory_order_acq_rel;
    using std::memory_order_seq_cst;

    using std::atomic_int_least64_t;
#else /* for std C compiler */
    #ifdef _MSC_VER /* for MSVC as it doesn’t support stdatomic.h */
        /*
         * MSVC does not support stdatomic.h so we have to use a volatile
         * variable and Interlock* methods
         *
         * MSVC treats volatile variables differently by default and automatically
         * provides higher memory ordering than an ISO compliant volatile.
         *
         * See link bellow for more information.
         * https://docs.microsoft.com/en-us/cpp/cpp/volatile-cpp?view=vs-2017
         */
        #define WIN32_LEAN_AND_MEAN
        #include <Windows.h>

        typedef volatile int64_t atomic_int_least64_t;
        #define memory_order_relaxed 0

        _Bool atomic_compare_exchange(volatile atomic_int_least64_t *obj,
                                            int64_t* expected, int64_t desired);

        #define atomic_load_explicit(loadAdd, memory_order) (*loadAdd)
        #define atomic_compare_exchange_weak_explicit( \
                obj, expected, desired, succ, fail)    \
            atomic_compare_exchange(obj, expected, desired)
        #define atomic_fetch_add_explicit(obj, arg, memory_order) \
            InterlockedExchangeAdd64(obj, arg)
        #define atomic_store_explicit(obj, arg, memory_order) \
            InterlockedExchange64(obj, arg)
        #define atomic_store(obj, arg) InterlockedExchange64(obj, arg)
    #else /* for std C11 compiler */
        #include <stdatomic.h>
    #endif
#endif

struct hdr_histogram
{
    // Write Once Read meany cache line 1
    int64_t lowest_trackable_value;
    int64_t highest_trackable_value;
    int32_t unit_magnitude;
    int32_t significant_figures;
    int32_t sub_bucket_half_count_magnitude;
    int32_t sub_bucket_half_count;
    int64_t sub_bucket_mask;
    int32_t sub_bucket_count;
    int32_t bucket_count;
    int32_t normalizing_index_offset;
    int32_t counts_len;
    atomic_int_least64_t* counts;
    // cache line 2
    uint8_t cache_line_padding[48];
    double conversion_ratio;
    atomic_int_least64_t total_count;
    // cache line 3
    uint8_t cache_line_padding_2[48];
    atomic_int_least64_t min_value;
    atomic_int_least64_t max_value;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocate the memory and initialise the hdr_histogram.
 *
 * Due to the size of the histogram being the result of some reasonably
 * involved math on the input parameters this function it is tricky to stack allocate.
 * The histogram should be released with hdr_close
 *
 * @param lowest_trackable_value The smallest possible value to be put into the
 * histogram.
 * @param highest_trackable_value The largest possible value to be put into the
 * histogram.
 * @param significant_figures The level of precision for this histogram, i.e. the number
 * of figures in a decimal number that will be maintained.  E.g. a value of 3 will mean
 * the results from the histogram will be accurate up to the first three digits.  Must
 * be a value between 1 and 5 (inclusive).
 * @param result Output parameter to capture allocated histogram.
 * @return 0 on success, EINVAL if lowest_trackable_value is < 1 or the
 * significant_figure value is outside of the allowed range, ENOMEM if malloc
 * failed.
 */
int hdr_init(
    int64_t lowest_trackable_value,
    int64_t highest_trackable_value,
    int significant_figures,
    struct hdr_histogram** result);

/**
 * Allocate the memory using custom calloc and initialise the hdr_histogram.
 *
 * Due to the size of the histogram being the result of some reasonably
 * involved math on the input parameters this function it is tricky to stack allocate.
 * The histogram should be released with hdr_close
 *
 * @param lowest_trackable_value The smallest possible value to be put into the
 * histogram.
 * @param highest_trackable_value The largest possible value to be put into the
 * histogram.
 * @param significant_figures The level of precision for this histogram, i.e. the number
 * of figures in a decimal number that will be maintained.  E.g. a value of 3 will mean
 * the results from the histogram will be accurate up to the first three digits.  Must
 * be a value between 1 and 5 (inclusive).
 * @param result Output parameter to capture allocated histogram.
 * @param custom_calloc function allocate memory for the histogram and
 * mem set to 0. 
 * @return 0 on success, EINVAL if lowest_trackable_value is < 1 or the
 * significant_figure value is outside of the allowed range, ENOMEM if malloc
 * failed.
 */
int hdr_init_ex(
        int64_t lowest_trackable_value,
        int64_t highest_trackable_value,
        int significant_figures,
        struct hdr_histogram** result,
        void* (*custom_calloc) (size_t num, size_t size));

/**
 * Free the memory and close the hdr_histogram.
 *
 * @param h The histogram you want to close.
 */
void hdr_close(struct hdr_histogram* h);

/**
 * Free the memory and close the hdr_histogram.
 *
 * @param h The histogram you want to close.
 * @param custom_free to be used on memory allocated by custom_calloc
 */
void hdr_close_ex(struct hdr_histogram* h, void (*custom_free)(void* memPtr));

/**
 * Allocate the memory and initialise the hdr_histogram.  This is the equivalent of calling
 * hdr_init(1, highest_trackable_value, significant_figures, result);
 *
 * @deprecated use hdr_init.
 */
int hdr_alloc(int64_t highest_trackable_value, int significant_figures, struct hdr_histogram** result);


/**
 * Reset a histogram to zero - empty out a histogram and re-initialise it
 *
 * If you want to re-use an existing histogram, but reset everything back to zero, this
 * is the routine to use.
 *
 * @param h The histogram you want to reset to empty.
 *
 */
void hdr_reset(struct hdr_histogram* h);

/**
 * Get the memory size of the hdr_histogram.
 *
 * @param h "This" pointer
 * @return The amount of memory used by the hdr_histogram in bytes
 */
size_t hdr_get_memory_size(struct hdr_histogram* h);

/**
 * Records a value in the histogram, will round this value of to a precision at or better
 * than the significant_figure specified at construction time.
 *
 * @param h "This" pointer
 * @param value Value to add to the histogram
 * @return false if the value is larger than the highest_trackable_value and can't be recorded,
 * true otherwise.
 */
bool hdr_record_value(struct hdr_histogram* h, int64_t value);

/**
 * Records count values in the histogram, will round this value of to a
 * precision at or better than the significant_figure specified at construction
 * time.
 *
 * @param h "This" pointer
 * @param value Value to add to the histogram
 * @param count Number of 'value's to add to the histogram
 * @return false if any value is larger than the highest_trackable_value and can't be recorded,
 * true otherwise.
 */
bool hdr_record_values(struct hdr_histogram* h, int64_t value, int64_t count);


/**
 * Record a value in the histogram and backfill based on an expected interval.
 *
 * Records a value in the histogram, will round this value of to a precision at or better
 * than the significant_figure specified at contruction time.  This is specifically used
 * for recording latency.  If the value is larger than the expected_interval then the
 * latency recording system has experienced co-ordinated omission.  This method fills in the
 * values that would have occured had the client providing the load not been blocked.

 * @param h "This" pointer
 * @param value Value to add to the histogram
 * @param expected_interval The delay between recording values.
 * @return false if the value is larger than the highest_trackable_value and can't be recorded,
 * true otherwise.
 */
bool hdr_record_corrected_value(struct hdr_histogram* h, int64_t value, int64_t expexcted_interval);
/**
 * Record a value in the histogram 'count' times.  Applies the same correcting logic
 * as 'hdr_record_corrected_value'.
 *
 * @param h "This" pointer
 * @param value Value to add to the histogram
 * @param count Number of 'value's to add to the histogram
 * @param expected_interval The delay between recording values.
 * @return false if the value is larger than the highest_trackable_value and can't be recorded,
 * true otherwise.
 */
bool hdr_record_corrected_values(struct hdr_histogram* h, int64_t value, int64_t count, int64_t expected_interval);

/**
 * Adds all of the values from 'from' to 'this' histogram.  Will return the
 * number of values that are dropped when copying.  Values will be dropped
 * if they around outside of h.lowest_trackable_value and
 * h.highest_trackable_value.
 *
 * @param h "This" pointer
 * @param from Histogram to copy values from.
 * @return The number of values dropped when copying.
 */
int64_t hdr_add(struct hdr_histogram* h, const struct hdr_histogram* from);

/**
 * Adds all of the values from 'from' to 'this' histogram.  Will return the
 * number of values that are dropped when copying.  Values will be dropped
 * if they around outside of h.lowest_trackable_value and
 * h.highest_trackable_value.
 *
 * @param h "This" pointer
 * @param from Histogram to copy values from.
 * @return The number of values dropped when copying.
 */
int64_t hdr_add_while_correcting_for_coordinated_omission(
    struct hdr_histogram* h, struct hdr_histogram* from, int64_t expected_interval);

/**
 * Get minimum value from the histogram.  Will return 2^63-1 if the histogram
 * is empty.
 *
 * @param h "This" pointer
 */
int64_t hdr_min(const struct hdr_histogram* h);

/**
 * Get maximum value from the histogram.  Will return 0 if the histogram
 * is empty.
 *
 * @param h "This" pointer
 */
int64_t hdr_max(const struct hdr_histogram* h);

/**
 * Get the value at a specific percentile.
 *
 * @param h "This" pointer.
 * @param percentile The percentile to get the value for
 */
int64_t hdr_value_at_percentile(const struct hdr_histogram* h, double percentile);

/**
 * Gets the standard deviation for the values in the histogram.
 *
 * @param h "This" pointer
 * @return The standard deviation
 */
double hdr_stddev(const struct hdr_histogram* h);

/**
 * Gets the mean for the values in the histogram.
 *
 * @param h "This" pointer
 * @return The mean
 */
double hdr_mean(const struct hdr_histogram* h);

/**
 * Determine if two values are equivalent with the histogram's resolution.
 * Where "equivalent" means that value samples recorded for any two
 * equivalent values are counted in a common total count.
 *
 * @param h "This" pointer
 * @param a first value to compare
 * @param b second value to compare
 * @return 'true' if values are equivalent with the histogram's resolution.
 */
bool hdr_values_are_equivalent(const struct hdr_histogram* h, int64_t a, int64_t b);

/**
 * Get the lowest value that is equivalent to the given value within the histogram's resolution.
 * Where "equivalent" means that value samples recorded for any two
 * equivalent values are counted in a common total count.
 *
 * @param h "This" pointer
 * @param value The given value
 * @return The lowest value that is equivalent to the given value within the histogram's resolution.
 */
int64_t hdr_lowest_equivalent_value(const struct hdr_histogram* h, int64_t value);

/**
 * Get the count of recorded values at a specific value
 * (to within the histogram resolution at the value level).
 *
 * @param h "This" pointer
 * @param value The value for which to provide the recorded count
 * @return The total count of values recorded in the histogram within the value range that is
 * {@literal >=} lowestEquivalentValue(<i>value</i>) and {@literal <=} highestEquivalentValue(<i>value</i>)
 */
int64_t hdr_count_at_value(const struct hdr_histogram* h, int64_t value);

int64_t hdr_count_at_index(const struct hdr_histogram* h, int32_t index);

int64_t hdr_value_at_index(const struct hdr_histogram* h, int32_t index);

struct hdr_iter_percentiles
{
    bool seen_last_value;
    int32_t ticks_per_half_distance;
    double percentile_to_iterate_to;
    double percentile;
};

struct hdr_iter_recorded
{
    int64_t count_added_in_this_iteration_step;
};

struct hdr_iter_linear
{
    int64_t value_units_per_bucket;
    int64_t count_added_in_this_iteration_step;
    int64_t next_value_reporting_level;
    int64_t next_value_reporting_level_lowest_equivalent;
};

struct hdr_iter_log
{
    double log_base;
    int64_t count_added_in_this_iteration_step;
    int64_t next_value_reporting_level;
    int64_t next_value_reporting_level_lowest_equivalent;
};

/**
 * The basic iterator.  This is a generic structure
 * that supports all of the types of iteration.  Use
 * the appropriate initialiser to get the desired
 * iteration.
 *
 * @
 */
struct hdr_iter
{
    const struct hdr_histogram* h;
    /** raw index into the counts array */
    int32_t counts_index;
    /** snapshot of the length at the time the iterator is created */
    int32_t total_count;
    /** value directly from array for the current counts_index */
    int64_t count;
    /** sum of all of the counts up to and including the count at this index */
    int64_t cumulative_count;
    /** The current value based on counts_index */
    int64_t value;
    int64_t highest_equivalent_value;
    int64_t lowest_equivalent_value;
    int64_t median_equivalent_value;
    int64_t value_iterated_from;
    int64_t value_iterated_to;

    union
    {
        struct hdr_iter_percentiles percentiles;
        struct hdr_iter_recorded recorded;
        struct hdr_iter_linear linear;
        struct hdr_iter_log log;
    } specifics;

    bool (* _next_fp)(struct hdr_iter* iter);

};

/**
 * Initalises the basic iterator.
 *
 * @param itr 'This' pointer
 * @param h The histogram to iterate over
 */
void hdr_iter_init(struct hdr_iter* iter, const struct hdr_histogram* h);

/**
 * Initialise the iterator for use with percentiles.
 */
void hdr_iter_percentile_init(struct hdr_iter* iter, const struct hdr_histogram* h, int32_t ticks_per_half_distance);

/**
 * Initialise the iterator for use with recorded values.
 */
void hdr_iter_recorded_init(struct hdr_iter* iter, const struct hdr_histogram* h);

/**
 * Initialise the iterator for use with linear values.
 */
void hdr_iter_linear_init(
    struct hdr_iter* iter,
    const struct hdr_histogram* h,
    int64_t value_units_per_bucket);

/**
 * Initialise the iterator for use with logarithmic values
 */
void hdr_iter_log_init(
    struct hdr_iter* iter,
    const struct hdr_histogram* h,
    int64_t value_units_first_bucket,
    double log_base);

/**
 * Iterate to the next value for the iterator.  If there are no more values
 * available return faluse.
 *
 * @param itr 'This' pointer
 * @return 'false' if there are no values remaining for this iterator.
 */
bool hdr_iter_next(struct hdr_iter* iter);

typedef enum
{
    CLASSIC,
    CSV
} format_type;

/**
 * Print out a percentile based histogram to the supplied stream.  Note that
 * this call will not flush the FILE, this is left up to the user.
 *
 * @param h 'This' pointer
 * @param stream The FILE to write the output to
 * @param ticks_per_half_distance The number of iteration steps per half-distance to 100%
 * @param value_scale Scale the output values by this amount
 * @param format_type Format to use, e.g. CSV.
 * @return 0 on success, error code on failure.  EIO if an error occurs writing
 * the output.
 */
int hdr_percentiles_print(
    struct hdr_histogram* h, FILE* stream, int32_t ticks_per_half_distance,
    double value_scale, format_type format);

/**
* Internal allocation methods, used by hdr_dbl_histogram.
*/
struct hdr_histogram_bucket_config
{
    int64_t lowest_trackable_value;
    int64_t highest_trackable_value;
    int64_t unit_magnitude;
    int64_t significant_figures;
    int32_t sub_bucket_half_count_magnitude;
    int32_t sub_bucket_half_count;
    int64_t sub_bucket_mask;
    int32_t sub_bucket_count;
    int32_t bucket_count;
    int32_t counts_len;
};

int hdr_calculate_bucket_config(
    int64_t lowest_trackable_value,
    int64_t highest_trackable_value,
    int significant_figures,
    struct hdr_histogram_bucket_config* cfg);

/**
 * Function to allocate memory that is aligned to a multiple of 128 alignment,
 * the memory allocated will also be zeroed
 *
 * @param num number of elements to be allocated
 * @param size of each element allocated
 */
void* hdr_aligned_calloc(size_t num, size_t size);

/**
 * Function to free memory that was align allocated using hdr_aligned_calloc
 *
 * @param memPtr pointer to memory to free
 */
void hdr_aligned_free(void* memPtr);

void hdr_init_preallocated(struct hdr_histogram* h, struct hdr_histogram_bucket_config* cfg);

int64_t hdr_size_of_equivalent_value_range(const struct hdr_histogram* h, int64_t value);

int64_t hdr_next_non_equivalent_value(const struct hdr_histogram* h, int64_t value);

int64_t hdr_median_equivalent_value(const struct hdr_histogram* h, int64_t value);

/**
 * Used to reset counters after importing data manuallying into the histogram, used by the logging code
 * and other custom serialisation tools.
 */
void hdr_reset_internal_counters(struct hdr_histogram* h);

#ifdef __cplusplus
}
#endif

#endif
