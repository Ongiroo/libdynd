//
// Copyright (C) 2011 Mark Wiebe (mwwiebe@gmail.com)
// All rights reserved.
//
// This is unreleased proprietary software.
//
#ifndef _SHORTVECTOR_HPP_
#define _SHORTVECTOR_HPP_

// For std::memcpy
#include <cstring>
#include <algorithm>
#include <type_traits>

namespace dnd {

/**
 * This is a helper class for storing vectors that are
 * usually short. It doesn't store the actual length of the
 * array, however. When the vector is of length N or shorter,
 * no memory is allocated on the heap, otherwise it is.
 *
 * The purpose of this is to allow arrays with fewer dimensions
 * to be manipulated without allocating memory on the heap
 * for the shape and strides, but without sacrificing full generality
 * in the number of dimensions.
 */
template<class T, int staticN = 3>
class shortvector {
    T *m_data;
    T m_shortdata[staticN];
public:
    /** Construct an uninitialized shortvector */
    shortvector()
        : m_data(m_shortdata) {
    }

    /** Initializes the shortvector after default construction */
    void init(int size) {
        if (m_data != m_shortdata) {
            delete[] m_data;
        }
        if (size <= staticN) {
            m_data = m_shortdata;
        } else {
            m_data = new T[size];
        }
    }

    /** Construct the shortvector with a specified size */
    explicit shortvector(int size) {
        if (size <= staticN) {
            m_data = m_shortdata;
        } else {
            m_data = new T[size];
        }
    }
    /** Construct the shortvector with a specified size and initial data */
    shortvector(int size, const shortvector& rhs) {
        // Could use C++11 delegating constructor for the first part
        if (size <= staticN) {
            m_data = m_shortdata;
        } else {
            m_data = new T[size];
        }
        std::memcpy(m_data, rhs.m_data, size * sizeof(T));
    }
    /** Construct the shortvector with a specified size and initial data */
    shortvector(int size, const T* data) {
        // Could use C++11 delegating constructor for the first part
        if (size <= staticN) {
            m_data = m_shortdata;
        } else {
            m_data = new T[size];
        }
        std::memcpy(m_data, data, size * sizeof(T));
    }
    /** Non-copyable */
    shortvector(const shortvector& rhs) = delete;
    /** Move constructor */
    shortvector(shortvector&& rhs) {
        if (rhs.m_data == rhs.m_shortdata) {
            // In the short case, copy the full shortdata vector
            std::memcpy(m_shortdata, rhs.m_shortdata, staticN * sizeof(T));
            m_data = m_shortdata;
        } else {
            // In the long case, move the long allocated pointer
            m_data = rhs.m_data;
            rhs.m_data = rhs.m_shortdata;
        }
    }
    /** Non-copyable (this object wouldn't know how much to allocate anyway) */
    shortvector& operator=(const shortvector& rhs) = delete;
    /** Move assignment operator */
    shortvector& operator=(shortvector&& rhs) {
        if (this != &rhs) {
            if (m_data != m_shortdata) {
                delete[] m_data;
            }
            if (rhs.m_data == rhs.m_shortdata) {
                // In the short case, copy the full shortdata vector
                std::memcpy(m_shortdata, rhs.m_shortdata, staticN * sizeof(T));
                m_data = m_shortdata;
            } else {
                // In the long case, move the long allocated pointer
                m_data = rhs.m_data;
                rhs.m_data = rhs.m_shortdata;
            }
        }
        return *this;
    }

    /** Destructor */
    ~shortvector() {
        if (m_data != m_shortdata) {
            delete[] m_data;
        }
    }

    void swap(shortvector& rhs) {
        // Start by swapping the pointers
        std::swap(m_data, rhs.m_data);
        // Copy the shortdata if necessary
        if (m_data == rhs.m_shortdata) {
            // The rhs data was pointing to shortdata
            m_data = m_shortdata;
            if (rhs.m_data == m_shortdata) {
                // Both data's were pointing to their shortdata
                T tmp[staticN];
                rhs.m_data = rhs.m_shortdata;
                std::memcpy(tmp, m_shortdata, staticN * sizeof(T));
                std::memcpy(m_shortdata, rhs.m_shortdata, staticN * sizeof(T));
                std::memcpy(rhs.m_shortdata, tmp, staticN * sizeof(T));
            } else {
                // Just the rhs data was pointing to shortdata
                std::memcpy(m_shortdata, rhs.m_shortdata, staticN * sizeof(T));
            }
        } else if (rhs.m_data == m_shortdata) {
            // Just this data was pointing to shortdata
            rhs.m_data = rhs.m_shortdata;
            std::memcpy(rhs.m_shortdata, m_shortdata, staticN * sizeof(T));
        }
    }

    /** Gets the const data pointer */
    const T* get() const {
        return m_data;
    }
    /** Gets the non-const data pointer */
    T* get() {
        return m_data;
    }

    /** Const indexing operator */
    const T& operator[](int i) const {
        return m_data[i];
    }
    /** Non-const indexing operator */
    T& operator[](int i) {
        return m_data[i];
    }
};

/**
 * This class holds an array of vectors, using
 * static storage if the number of elements is less
 * or equal to staticN. Using this class allows code
 * to combine what would be many small new[] calls into
 * one bigger call, when the number of shortvectors needed
 * is known ahead of time.
 *
 * To use this class, call the init() function with
 * the number of elements desired, then access the arrays
 * with get<subvector>(index).
 *
 * Before calling the init() function, the class is not
 * in a useable state.
 */
template<class T, int M, int staticN = 3>
class multi_shortvector {
    T *m_shortvectors[M];
    T m_static_data[staticN * M];
    T *m_alloc_data;

    void internal_init(int n) {
        if (n <= staticN) {
            // Do at minimum n == 1
            if (n == 0) {
                n = 1;
            }
            for (int i = 0; i < M; ++i) {
                m_shortvectors[i] = &m_static_data[i*n];
            }
        } else {
            m_alloc_data = new T[n * M];
            for (int i = 0; i < M; ++i) {
                m_shortvectors[i] = m_alloc_data + i*n;
            }
        }
    }
public:
    multi_shortvector()
        : m_alloc_data(NULL) {
    }

    multi_shortvector(int n)
        : m_alloc_data(NULL) {
        internal_init(n);
    }

    ~multi_shortvector() {
        // This is OK if m_alloc_data is NULL
        delete[] m_alloc_data;
    }

    void init(int n) {
        if (m_alloc_data != NULL) {
            delete[] m_alloc_data;
        }
        internal_init(n);
    }

    T **get_all() {
        return m_shortvectors;
    }

    const T * const*get_all() const {
        return m_shortvectors;
    }

    T *get(int i) {
        return m_shortvectors[i];
    }

    const T *get(int i) const {
        return m_shortvectors[i];
    }

    T& get(int i, int j) {
        return m_shortvectors[i][j];
    }

    const T& get(int i, int j) const {
        return m_shortvectors[i][j];
    }
};

/** Typedef for vector of dimensions or strides */
typedef shortvector<intptr_t> dimvector;

} // namespace dnd

#endif // _SHORTVECTOR_HPP_
