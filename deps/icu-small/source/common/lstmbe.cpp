// © 2021 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <complex>
#include <utility>

#include "unicode/utypes.h"

#if !UCONFIG_NO_BREAK_ITERATION

#include "brkeng.h"
#include "charstr.h"
#include "cmemory.h"
#include "lstmbe.h"
#include "putilimp.h"
#include "uassert.h"
#include "ubrkimpl.h"
#include "uresimp.h"
#include "uvectr32.h"
#include "uvector.h"

#include "unicode/brkiter.h"
#include "unicode/resbund.h"
#include "unicode/ubrk.h"
#include "unicode/uniset.h"
#include "unicode/ustring.h"
#include "unicode/utf.h"

U_NAMESPACE_BEGIN

// Uncomment the following #define to debug.
// #define LSTM_DEBUG 1
// #define LSTM_VECTORIZER_DEBUG 1

/**
 * Interface for reading 1D array.
 */
class ReadArray1D {
public:
    virtual ~ReadArray1D();
    virtual int32_t d1() const = 0;
    virtual float get(int32_t i) const = 0;

#ifdef LSTM_DEBUG
    void print() const {
        printf("\n[");
        for (int32_t i = 0; i < d1(); i++) {
           printf("%0.8e ", get(i));
           if (i % 4 == 3) printf("\n");
        }
        printf("]\n");
    }
#endif
};

ReadArray1D::~ReadArray1D()
{
}

/**
 * Interface for reading 2D array.
 */
class ReadArray2D {
public:
    virtual ~ReadArray2D();
    virtual int32_t d1() const = 0;
    virtual int32_t d2() const = 0;
    virtual float get(int32_t i, int32_t j) const = 0;
};

ReadArray2D::~ReadArray2D()
{
}

/**
 * A class to index a float array as a 1D Array without owning the pointer or
 * copy the data.
 */
class ConstArray1D : public ReadArray1D {
public:
    ConstArray1D() : data_(nullptr), d1_(0) {}

    ConstArray1D(const float* data, int32_t d1) : data_(data), d1_(d1) {}

    virtual ~ConstArray1D();

    // Init the object, the object does not own the data nor copy.
    // It is designed to directly use data from memory mapped resources.
    void init(const int32_t* data, int32_t d1) {
        U_ASSERT(IEEE_754 == 1);
        data_ = reinterpret_cast<const float*>(data);
        d1_ = d1;
    }

    // ReadArray1D methods.
    virtual int32_t d1() const override { return d1_; }
    virtual float get(int32_t i) const override {
        U_ASSERT(i < d1_);
        return data_[i];
    }

private:
    const float* data_;
    int32_t d1_;
};

ConstArray1D::~ConstArray1D()
{
}

/**
 * A class to index a float array as a 2D Array without owning the pointer or
 * copy the data.
 */
class ConstArray2D : public ReadArray2D {
public:
    ConstArray2D() : data_(nullptr), d1_(0), d2_(0) {}

    ConstArray2D(const float* data, int32_t d1, int32_t d2)
        : data_(data), d1_(d1), d2_(d2) {}

    virtual ~ConstArray2D();

    // Init the object, the object does not own the data nor copy.
    // It is designed to directly use data from memory mapped resources.
    void init(const int32_t* data, int32_t d1, int32_t d2) {
        U_ASSERT(IEEE_754 == 1);
        data_ = reinterpret_cast<const float*>(data);
        d1_ = d1;
        d2_ = d2;
    }

    // ReadArray2D methods.
    inline int32_t d1() const override { return d1_; }
    inline int32_t d2() const override { return d2_; }
    float get(int32_t i, int32_t j) const override {
        U_ASSERT(i < d1_);
        U_ASSERT(j < d2_);
        return data_[i * d2_ + j];
    }

    // Expose the ith row as a ConstArray1D
    inline ConstArray1D row(int32_t i) const {
        U_ASSERT(i < d1_);
        return ConstArray1D(data_ + i * d2_, d2_);
    }

private:
    const float* data_;
    int32_t d1_;
    int32_t d2_;
};

ConstArray2D::~ConstArray2D()
{
}

/**
 * A class to allocate data as a writable 1D array.
 * This is the main class implement matrix operation.
 */
class Array1D : public ReadArray1D {
public:
    Array1D() : memory_(nullptr), data_(nullptr), d1_(0) {}
    Array1D(int32_t d1, UErrorCode &status)
        : memory_(uprv_malloc(d1 * sizeof(float))),
          data_(static_cast<float*>(memory_)), d1_(d1) {
        if (U_SUCCESS(status)) {
            if (memory_ == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            clear();
        }
    }

    virtual ~Array1D();

    // A special constructor which does not own the memory but writeable
    // as a slice of an array.
    Array1D(float* data, int32_t d1)
        : memory_(nullptr), data_(data), d1_(d1) {}

    // ReadArray1D methods.
    virtual int32_t d1() const override { return d1_; }
    virtual float get(int32_t i) const override {
        U_ASSERT(i < d1_);
        return data_[i];
    }

    // Return the index which point to the max data in the array.
    inline int32_t maxIndex() const {
        int32_t index = 0;
        float max = data_[0];
        for (int32_t i = 1; i < d1_; i++) {
            if (data_[i] > max) {
                max = data_[i];
                index = i;
            }
        }
        return index;
    }

    // Slice part of the array to a new one.
    inline Array1D slice(int32_t from, int32_t size) const {
        U_ASSERT(from >= 0);
        U_ASSERT(from < d1_);
        U_ASSERT(from + size <= d1_);
        return Array1D(data_ + from, size);
    }

    // Add dot product of a 1D array and a 2D array into this one.
    inline Array1D& addDotProduct(const ReadArray1D& a, const ReadArray2D& b) {
        U_ASSERT(a.d1() == b.d1());
        U_ASSERT(b.d2() == d1());
        for (int32_t i = 0; i < d1(); i++) {
            for (int32_t j = 0; j < a.d1(); j++) {
                data_[i] += a.get(j) * b.get(j, i);
            }
        }
        return *this;
    }

    // Hadamard Product the values of another array of the same size into this one.
    inline Array1D& hadamardProduct(const ReadArray1D& a) {
        U_ASSERT(a.d1() == d1());
        for (int32_t i = 0; i < d1(); i++) {
            data_[i] *= a.get(i);
        }
        return *this;
    }

    // Add the Hadamard Product of two arrays of the same size into this one.
    inline Array1D& addHadamardProduct(const ReadArray1D& a, const ReadArray1D& b) {
        U_ASSERT(a.d1() == d1());
        U_ASSERT(b.d1() == d1());
        for (int32_t i = 0; i < d1(); i++) {
            data_[i] += a.get(i) * b.get(i);
        }
        return *this;
    }

    // Add the values of another array of the same size into this one.
    inline Array1D& add(const ReadArray1D& a) {
        U_ASSERT(a.d1() == d1());
        for (int32_t i = 0; i < d1(); i++) {
            data_[i] += a.get(i);
        }
        return *this;
    }

    // Assign the values of another array of the same size into this one.
    inline Array1D& assign(const ReadArray1D& a) {
        U_ASSERT(a.d1() == d1());
        for (int32_t i = 0; i < d1(); i++) {
            data_[i] = a.get(i);
        }
        return *this;
    }

    // Apply tanh to all the elements in the array.
    inline Array1D& tanh() {
        return tanh(*this);
    }

    // Apply tanh of a and store into this array.
    inline Array1D& tanh(const Array1D& a) {
        U_ASSERT(a.d1() == d1());
        for (int32_t i = 0; i < d1_; i++) {
            data_[i] = std::tanh(a.get(i));
        }
        return *this;
    }

    // Apply sigmoid to all the elements in the array.
    inline Array1D& sigmoid() {
        for (int32_t i = 0; i < d1_; i++) {
            data_[i] = 1.0f/(1.0f + expf(-data_[i]));
        }
        return *this;
    }

    inline Array1D& clear() {
        uprv_memset(data_, 0, d1_ * sizeof(float));
        return *this;
    }

private:
    void* memory_;
    float* data_;
    int32_t d1_;
};

Array1D::~Array1D()
{
    uprv_free(memory_);
}

class Array2D : public ReadArray2D {
public:
    Array2D() : memory_(nullptr), data_(nullptr), d1_(0), d2_(0) {}
    Array2D(int32_t d1, int32_t d2, UErrorCode &status)
        : memory_(uprv_malloc(d1 * d2 * sizeof(float))),
          data_(static_cast<float*>(memory_)), d1_(d1), d2_(d2) {
        if (U_SUCCESS(status)) {
            if (memory_ == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            clear();
        }
    }
    virtual ~Array2D();

    // ReadArray2D methods.
    virtual int32_t d1() const override { return d1_; }
    virtual int32_t d2() const override { return d2_; }
    virtual float get(int32_t i, int32_t j) const override {
        U_ASSERT(i < d1_);
        U_ASSERT(j < d2_);
        return data_[i * d2_ + j];
    }

    inline Array1D row(int32_t i) const {
        U_ASSERT(i < d1_);
        return Array1D(data_ + i * d2_, d2_);
    }

    inline Array2D& clear() {
        uprv_memset(data_, 0, d1_ * d2_ * sizeof(float));
        return *this;
    }

private:
    void* memory_;
    float* data_;
    int32_t d1_;
    int32_t d2_;
};

Array2D::~Array2D()
{
    uprv_free(memory_);
}

typedef enum {
    BEGIN,
    INSIDE,
    END,
    SINGLE
} LSTMClass;

typedef enum {
    UNKNOWN,
    CODE_POINTS,
    GRAPHEME_CLUSTER,
} EmbeddingType;

struct LSTMData : public UMemory {
    LSTMData(UResourceBundle* rb, UErrorCode &status);
    ~LSTMData();
    UHashtable* fDict;
    EmbeddingType fType;
    const char16_t* fName;
    ConstArray2D fEmbedding;
    ConstArray2D fForwardW;
    ConstArray2D fForwardU;
    ConstArray1D fForwardB;
    ConstArray2D fBackwardW;
    ConstArray2D fBackwardU;
    ConstArray1D fBackwardB;
    ConstArray2D fOutputW;
    ConstArray1D fOutputB;

private:
    UResourceBundle* fBundle;
};

LSTMData::LSTMData(UResourceBundle* rb, UErrorCode &status)
    : fDict(nullptr), fType(UNKNOWN), fName(nullptr),
      fBundle(rb)
{
    if (U_FAILURE(status)) {
        return;
    }
    if (IEEE_754 != 1) {
        status = U_UNSUPPORTED_ERROR;
        return;
    }
    LocalUResourceBundlePointer embeddings_res(
        ures_getByKey(rb, "embeddings", nullptr, &status));
    int32_t embedding_size = ures_getInt(embeddings_res.getAlias(), &status);
    LocalUResourceBundlePointer hunits_res(
        ures_getByKey(rb, "hunits", nullptr, &status));
    if (U_FAILURE(status)) return;
    int32_t hunits = ures_getInt(hunits_res.getAlias(), &status);
    const char16_t* type = ures_getStringByKey(rb, "type", nullptr, &status);
    if (U_FAILURE(status)) return;
    if (u_strCompare(type, -1, u"codepoints", -1, false) == 0) {
        fType = CODE_POINTS;
    } else if (u_strCompare(type, -1, u"graphclust", -1, false) == 0) {
        fType = GRAPHEME_CLUSTER;
    }
    fName = ures_getStringByKey(rb, "model", nullptr, &status);
    LocalUResourceBundlePointer dataRes(ures_getByKey(rb, "data", nullptr, &status));
    if (U_FAILURE(status)) return;
    int32_t data_len = 0;
    const int32_t* data = ures_getIntVector(dataRes.getAlias(), &data_len, &status);
    fDict = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);

    StackUResourceBundle stackTempBundle;
    ResourceDataValue value;
    ures_getValueWithFallback(rb, "dict", stackTempBundle.getAlias(), value, status);
    ResourceArray stringArray = value.getArray(status);
    int32_t num_index = stringArray.getSize();
    if (U_FAILURE(status)) { return; }

    // put dict into hash
    int32_t stringLength;
    for (int32_t idx = 0; idx < num_index; idx++) {
        stringArray.getValue(idx, value);
        const char16_t* str = value.getString(stringLength, status);
        uhash_putiAllowZero(fDict, (void*)str, idx, &status);
        if (U_FAILURE(status)) return;
#ifdef LSTM_VECTORIZER_DEBUG
        printf("Assign [");
        while (*str != 0x0000) {
            printf("U+%04x ", *str);
            str++;
        }
        printf("] map to %d\n", idx-1);
#endif
    }
    int32_t mat1_size = (num_index + 1) * embedding_size;
    int32_t mat2_size = embedding_size * 4 * hunits;
    int32_t mat3_size = hunits * 4 * hunits;
    int32_t mat4_size = 4 * hunits;
    int32_t mat5_size = mat2_size;
    int32_t mat6_size = mat3_size;
    int32_t mat7_size = mat4_size;
    int32_t mat8_size = 2 * hunits * 4;
#if U_DEBUG
    int32_t mat9_size = 4;
    U_ASSERT(data_len == mat1_size + mat2_size + mat3_size + mat4_size + mat5_size +
        mat6_size + mat7_size + mat8_size + mat9_size);
#endif

    fEmbedding.init(data, (num_index + 1), embedding_size);
    data += mat1_size;
    fForwardW.init(data, embedding_size, 4 * hunits);
    data += mat2_size;
    fForwardU.init(data, hunits, 4 * hunits);
    data += mat3_size;
    fForwardB.init(data, 4 * hunits);
    data += mat4_size;
    fBackwardW.init(data, embedding_size, 4 * hunits);
    data += mat5_size;
    fBackwardU.init(data, hunits, 4 * hunits);
    data += mat6_size;
    fBackwardB.init(data, 4 * hunits);
    data += mat7_size;
    fOutputW.init(data, 2 * hunits, 4);
    data += mat8_size;
    fOutputB.init(data, 4);
}

LSTMData::~LSTMData() {
    uhash_close(fDict);
    ures_close(fBundle);
}

class Vectorizer : public UMemory {
public:
    Vectorizer(UHashtable* dict) : fDict(dict) {}
    virtual ~Vectorizer();
    virtual void vectorize(UText *text, int32_t startPos, int32_t endPos,
                           UVector32 &offsets, UVector32 &indices,
                           UErrorCode &status) const = 0;
protected:
    int32_t stringToIndex(const char16_t* str) const {
        UBool found = false;
        int32_t ret = uhash_getiAndFound(fDict, (const void*)str, &found);
        if (!found) {
            ret = fDict->count;
        }
#ifdef LSTM_VECTORIZER_DEBUG
        printf("[");
        while (*str != 0x0000) {
            printf("U+%04x ", *str);
            str++;
        }
        printf("] map to %d\n", ret);
#endif
        return ret;
    }

private:
    UHashtable* fDict;
};

Vectorizer::~Vectorizer()
{
}

class CodePointsVectorizer : public Vectorizer {
public:
    CodePointsVectorizer(UHashtable* dict) : Vectorizer(dict) {}
    virtual ~CodePointsVectorizer();
    virtual void vectorize(UText *text, int32_t startPos, int32_t endPos,
                           UVector32 &offsets, UVector32 &indices,
                           UErrorCode &status) const override;
};

CodePointsVectorizer::~CodePointsVectorizer()
{
}

void CodePointsVectorizer::vectorize(
    UText *text, int32_t startPos, int32_t endPos,
    UVector32 &offsets, UVector32 &indices, UErrorCode &status) const
{
    if (offsets.ensureCapacity(endPos - startPos, status) &&
            indices.ensureCapacity(endPos - startPos, status)) {
        if (U_FAILURE(status)) return;
        utext_setNativeIndex(text, startPos);
        int32_t current;
        char16_t str[2] = {0, 0};
        while (U_SUCCESS(status) &&
               (current = static_cast<int32_t>(utext_getNativeIndex(text))) < endPos) {
            // Since the LSTMBreakEngine is currently only accept chars in BMP,
            // we can ignore the possibility of hitting supplementary code
            // point.
            str[0] = static_cast<char16_t>(utext_next32(text));
            U_ASSERT(!U_IS_SURROGATE(str[0]));
            offsets.addElement(current, status);
            indices.addElement(stringToIndex(str), status);
        }
    }
}

class GraphemeClusterVectorizer : public Vectorizer {
public:
    GraphemeClusterVectorizer(UHashtable* dict)
        : Vectorizer(dict)
    {
    }
    virtual ~GraphemeClusterVectorizer();
    virtual void vectorize(UText *text, int32_t startPos, int32_t endPos,
                           UVector32 &offsets, UVector32 &indices,
                           UErrorCode &status) const override;
};

GraphemeClusterVectorizer::~GraphemeClusterVectorizer()
{
}

constexpr int32_t MAX_GRAPHEME_CLSTER_LENGTH = 10;

void GraphemeClusterVectorizer::vectorize(
    UText *text, int32_t startPos, int32_t endPos,
    UVector32 &offsets, UVector32 &indices, UErrorCode &status) const
{
    if (U_FAILURE(status)) return;
    if (!offsets.ensureCapacity(endPos - startPos, status) ||
            !indices.ensureCapacity(endPos - startPos, status)) {
        return;
    }
    if (U_FAILURE(status)) return;
    LocalPointer<BreakIterator> graphemeIter(BreakIterator::createCharacterInstance(Locale(), status));
    if (U_FAILURE(status)) return;
    graphemeIter->setText(text, status);
    if (U_FAILURE(status)) return;

    if (startPos != 0) {
        graphemeIter->preceding(startPos);
    }
    int32_t last = startPos;
    int32_t current = startPos;
    char16_t str[MAX_GRAPHEME_CLSTER_LENGTH];
    while ((current = graphemeIter->next()) != BreakIterator::DONE) {
        if (current >= endPos) {
            break;
        }
        if (current > startPos) {
            utext_extract(text, last, current, str, MAX_GRAPHEME_CLSTER_LENGTH, &status);
            if (U_FAILURE(status)) return;
            offsets.addElement(last, status);
            indices.addElement(stringToIndex(str), status);
            if (U_FAILURE(status)) return;
        }
        last = current;
    }
    if (U_FAILURE(status) || last >= endPos) {
        return;
    }
    utext_extract(text, last, endPos, str, MAX_GRAPHEME_CLSTER_LENGTH, &status);
    if (U_SUCCESS(status)) {
        offsets.addElement(last, status);
        indices.addElement(stringToIndex(str), status);
    }
}

// Computing LSTM as stated in
// https://en.wikipedia.org/wiki/Long_short-term_memory#LSTM_with_a_forget_gate
// ifco is temp array allocate outside which does not need to be
// input/output value but could avoid unnecessary memory alloc/free if passing
// in.
void compute(
    int32_t hunits,
    const ReadArray2D& W, const ReadArray2D& U, const ReadArray1D& b,
    const ReadArray1D& x, Array1D& h, Array1D& c,
    Array1D& ifco)
{
    // ifco = x * W + h * U + b
    ifco.assign(b)
        .addDotProduct(x, W)
        .addDotProduct(h, U);

    ifco.slice(0*hunits, hunits).sigmoid();  // i: sigmod
    ifco.slice(1*hunits, hunits).sigmoid(); // f: sigmoid
    ifco.slice(2*hunits, hunits).tanh(); // c_: tanh
    ifco.slice(3*hunits, hunits).sigmoid(); // o: sigmod

    c.hadamardProduct(ifco.slice(hunits, hunits))
        .addHadamardProduct(ifco.slice(0, hunits), ifco.slice(2*hunits, hunits));

    h.tanh(c)
        .hadamardProduct(ifco.slice(3*hunits, hunits));
}

// Minimum word size
static const int32_t MIN_WORD = 2;

// Minimum number of characters for two words
static const int32_t MIN_WORD_SPAN = MIN_WORD * 2;

int32_t
LSTMBreakEngine::divideUpDictionaryRange( UText *text,
                                                int32_t startPos,
                                                int32_t endPos,
                                                UVector32 &foundBreaks,
                                                UBool /* isPhraseBreaking */,
                                                UErrorCode& status) const {
    if (U_FAILURE(status)) return 0;
    int32_t beginFoundBreakSize = foundBreaks.size();
    utext_setNativeIndex(text, startPos);
    utext_moveIndex32(text, MIN_WORD_SPAN);
    if (utext_getNativeIndex(text) >= endPos) {
        return 0;       // Not enough characters for two words
    }
    utext_setNativeIndex(text, startPos);

    UVector32 offsets(status);
    UVector32 indices(status);
    if (U_FAILURE(status)) return 0;
    fVectorizer->vectorize(text, startPos, endPos, offsets, indices, status);
    if (U_FAILURE(status)) return 0;
    int32_t* offsetsBuf = offsets.getBuffer();
    int32_t* indicesBuf = indices.getBuffer();

    int32_t input_seq_len = indices.size();
    int32_t hunits = fData->fForwardU.d1();

    // ----- Begin of all the Array memory allocation needed for this function
    // Allocate temp array used inside compute()
    Array1D ifco(4 * hunits, status);

    Array1D c(hunits, status);
    Array1D logp(4, status);

    // TODO: limit size of hBackward. If input_seq_len is too big, we could
    // run out of memory.
    // Backward LSTM
    Array2D hBackward(input_seq_len, hunits, status);

    // Allocate fbRow and slice the internal array in two.
    Array1D fbRow(2 * hunits, status);

    // ----- End of all the Array memory allocation needed for this function
    if (U_FAILURE(status)) return 0;

    // To save the needed memory usage, the following is different from the
    // Python or ICU4X implementation. We first perform the Backward LSTM
    // and then merge the iteration of the forward LSTM and the output layer
    // together because we only neetdto remember the h[t-1] for Forward LSTM.
    for (int32_t i = input_seq_len - 1; i >= 0; i--) {
        Array1D hRow = hBackward.row(i);
        if (i != input_seq_len - 1) {
            hRow.assign(hBackward.row(i+1));
        }
#ifdef LSTM_DEBUG
        printf("hRow %d\n", i);
        hRow.print();
        printf("indicesBuf[%d] = %d\n", i, indicesBuf[i]);
        printf("fData->fEmbedding.row(indicesBuf[%d]):\n", i);
        fData->fEmbedding.row(indicesBuf[i]).print();
#endif  // LSTM_DEBUG
        compute(hunits,
                fData->fBackwardW, fData->fBackwardU, fData->fBackwardB,
                fData->fEmbedding.row(indicesBuf[i]),
                hRow, c, ifco);
    }


    Array1D forwardRow = fbRow.slice(0, hunits);  // point to first half of data in fbRow.
    Array1D backwardRow = fbRow.slice(hunits, hunits);  // point to second half of data n fbRow.

    // The following iteration merge the forward LSTM and the output layer
    // together.
    c.clear();  // reuse c since it is the same size.
    for (int32_t i = 0; i < input_seq_len; i++) {
#ifdef LSTM_DEBUG
        printf("forwardRow %d\n", i);
        forwardRow.print();
#endif  // LSTM_DEBUG
        // Forward LSTM
        // Calculate the result into forwardRow, which point to the data in the first half
        // of fbRow.
        compute(hunits,
                fData->fForwardW, fData->fForwardU, fData->fForwardB,
                fData->fEmbedding.row(indicesBuf[i]),
                forwardRow, c, ifco);

        // assign the data from hBackward.row(i) to second half of fbRowa.
        backwardRow.assign(hBackward.row(i));

        logp.assign(fData->fOutputB).addDotProduct(fbRow, fData->fOutputW);
#ifdef LSTM_DEBUG
        printf("backwardRow %d\n", i);
        backwardRow.print();
        printf("logp %d\n", i);
        logp.print();
#endif  // LSTM_DEBUG

        // current = argmax(logp)
        LSTMClass current = static_cast<LSTMClass>(logp.maxIndex());
        // BIES logic.
        if (current == BEGIN || current == SINGLE) {
            if (i != 0) {
                foundBreaks.addElement(offsetsBuf[i], status);
                if (U_FAILURE(status)) return 0;
            }
        }
    }
    return foundBreaks.size() - beginFoundBreakSize;
}

Vectorizer* createVectorizer(const LSTMData* data, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    switch (data->fType) {
        case CODE_POINTS:
            return new CodePointsVectorizer(data->fDict);
            break;
        case GRAPHEME_CLUSTER:
            return new GraphemeClusterVectorizer(data->fDict);
            break;
        default:
            break;
    }
    UPRV_UNREACHABLE_EXIT;
}

LSTMBreakEngine::LSTMBreakEngine(const LSTMData* data, const UnicodeSet& set, UErrorCode &status)
    : DictionaryBreakEngine(), fData(data), fVectorizer(createVectorizer(fData, status))
{
    if (U_FAILURE(status)) {
      fData = nullptr;  // If failure, we should not delete fData in destructor because the caller will do so.
      return;
    }
    setCharacters(set);
}

LSTMBreakEngine::~LSTMBreakEngine() {
    delete fData;
    delete fVectorizer;
}

const char16_t* LSTMBreakEngine::name() const {
    return fData->fName;
}

UnicodeString defaultLSTM(UScriptCode script, UErrorCode& status) {
    // open root from brkitr tree.
    UResourceBundle *b = ures_open(U_ICUDATA_BRKITR, "", &status);
    b = ures_getByKeyWithFallback(b, "lstm", b, &status);
    UnicodeString result = ures_getUnicodeStringByKey(b, uscript_getShortName(script), &status);
    ures_close(b);
    return result;
}

U_CAPI const LSTMData* U_EXPORT2 CreateLSTMDataForScript(UScriptCode script, UErrorCode& status)
{
    if (script != USCRIPT_KHMER && script != USCRIPT_LAO && script != USCRIPT_MYANMAR && script != USCRIPT_THAI) {
        return nullptr;
    }
    UnicodeString name = defaultLSTM(script, status);
    if (U_FAILURE(status)) return nullptr;
    CharString namebuf;
    namebuf.appendInvariantChars(name, status).truncate(namebuf.lastIndexOf('.'));

    LocalUResourceBundlePointer rb(
        ures_openDirect(U_ICUDATA_BRKITR, namebuf.data(), &status));
    if (U_FAILURE(status)) return nullptr;

    return CreateLSTMData(rb.orphan(), status);
}

U_CAPI const LSTMData* U_EXPORT2 CreateLSTMData(UResourceBundle* rb, UErrorCode& status)
{
    return new LSTMData(rb, status);
}

U_CAPI const LanguageBreakEngine* U_EXPORT2
CreateLSTMBreakEngine(UScriptCode script, const LSTMData* data, UErrorCode& status)
{
    UnicodeString unicodeSetString;
    switch(script) {
        case USCRIPT_THAI:
            unicodeSetString = UnicodeString(u"[[:Thai:]&[:LineBreak=SA:]]");
            break;
        case USCRIPT_MYANMAR:
            unicodeSetString = UnicodeString(u"[[:Mymr:]&[:LineBreak=SA:]]");
            break;
        default:
            delete data;
            return nullptr;
    }
    UnicodeSet unicodeSet;
    unicodeSet.applyPattern(unicodeSetString, status);
    const LanguageBreakEngine* engine = new LSTMBreakEngine(data, unicodeSet, status);
    if (U_FAILURE(status) || engine == nullptr) {
        if (engine != nullptr) {
            delete engine;
        } else {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return nullptr;
    }
    return engine;
}

U_CAPI void U_EXPORT2 DeleteLSTMData(const LSTMData* data)
{
    delete data;
}

U_CAPI const char16_t* U_EXPORT2 LSTMDataName(const LSTMData* data)
{
    return data->fName;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_BREAK_ITERATION */
