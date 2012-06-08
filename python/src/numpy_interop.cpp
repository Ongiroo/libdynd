#include "numpy_interop.hpp"

#if DND_NUMPY_INTEROP

#include "dtype_functions.hpp"
#include "ndarray_functions.hpp"

#include <numpy/arrayscalars.h>

using namespace std;
using namespace dnd;
using namespace pydnd;

dtype pydnd::dtype_from_numpy_dtype(PyArray_Descr *d)
{
    if (!PyArray_ISNBO(d->byteorder)) {
        throw runtime_error("non-native byte order isn't supported yet by pydnd");
    }

    switch (d->type_num) {
    case NPY_BOOL:
        return make_dtype<dnd_bool>();
    case NPY_BYTE:
        return make_dtype<npy_byte>();
    case NPY_UBYTE:
        return make_dtype<npy_ubyte>();
    case NPY_SHORT:
        return make_dtype<npy_short>();
    case NPY_USHORT:
        return make_dtype<npy_ushort>();
    case NPY_INT:
        return make_dtype<npy_int>();
    case NPY_UINT:
        return make_dtype<npy_uint>();
    case NPY_LONG:
        return make_dtype<npy_long>();
    case NPY_ULONG:
        return make_dtype<npy_ulong>();
    case NPY_LONGLONG:
        return make_dtype<npy_longlong>();
    case NPY_ULONGLONG:
        return make_dtype<npy_ulonglong>();
    case NPY_FLOAT:
        return make_dtype<float>();
    case NPY_DOUBLE:
        return make_dtype<double>();
    case NPY_CFLOAT:
        return make_dtype<complex<float> >();
    case NPY_CDOUBLE:
        return make_dtype<complex<double> >();
    }

    stringstream ss;
    ss << "unsupported Numpy dtype with type id " << d->type_num;
    throw runtime_error(ss.str());
}

int pydnd::dtype_from_numpy_scalar_typeobject(PyTypeObject* obj, dnd::dtype& out_d)
{
    if (obj == &PyBoolArrType_Type) {
        out_d = make_dtype<dnd_bool>();
    } else if (obj == &PyByteArrType_Type) {
        out_d = make_dtype<npy_byte>();
    } else if (obj == &PyUByteArrType_Type) {
        out_d = make_dtype<npy_ubyte>();
    } else if (obj == &PyShortArrType_Type) {
        out_d = make_dtype<npy_short>();
    } else if (obj == &PyUShortArrType_Type) {
        out_d = make_dtype<npy_ushort>();
    } else if (obj == &PyIntArrType_Type) {
        out_d = make_dtype<npy_int>();
    } else if (obj == &PyUIntArrType_Type) {
        out_d = make_dtype<npy_uint>();
    } else if (obj == &PyLongArrType_Type) {
        out_d = make_dtype<npy_long>();
    } else if (obj == &PyULongArrType_Type) {
        out_d = make_dtype<npy_ulong>();
    } else if (obj == &PyLongLongArrType_Type) {
        out_d = make_dtype<npy_longlong>();
    } else if (obj == &PyULongLongArrType_Type) {
        out_d = make_dtype<npy_ulonglong>();
    } else if (obj == &PyFloatArrType_Type) {
        out_d = make_dtype<npy_float>();
    } else if (obj == &PyDoubleArrType_Type) {
        out_d = make_dtype<npy_double>();
    } else if (obj == &PyCFloatArrType_Type) {
        out_d = make_dtype<complex<float> >();
    } else if (obj == &PyCDoubleArrType_Type) {
        out_d = make_dtype<complex<double> >();
    } else {
        return -1;
    }

    return 0;
}

dtype pydnd::dtype_of_numpy_scalar(PyObject* obj)
{
    if (PyArray_IsScalar(obj, Bool)) {
        return make_dtype<dnd_bool>();
    } else if (PyArray_IsScalar(obj, Byte)) {
        return make_dtype<npy_byte>();
    } else if (PyArray_IsScalar(obj, UByte)) {
        return make_dtype<npy_ubyte>();
    } else if (PyArray_IsScalar(obj, Short)) {
        return make_dtype<npy_short>();
    } else if (PyArray_IsScalar(obj, UShort)) {
        return make_dtype<npy_ushort>();
    } else if (PyArray_IsScalar(obj, Int)) {
        return make_dtype<npy_int>();
    } else if (PyArray_IsScalar(obj, UInt)) {
        return make_dtype<npy_uint>();
    } else if (PyArray_IsScalar(obj, Long)) {
        return make_dtype<npy_long>();
    } else if (PyArray_IsScalar(obj, ULong)) {
        return make_dtype<npy_ulong>();
    } else if (PyArray_IsScalar(obj, LongLong)) {
        return make_dtype<npy_longlong>();
    } else if (PyArray_IsScalar(obj, ULongLong)) {
        return make_dtype<npy_ulonglong>();
    } else if (PyArray_IsScalar(obj, Float)) {
        return make_dtype<float>();
    } else if (PyArray_IsScalar(obj, Double)) {
        return make_dtype<double>();
    } else if (PyArray_IsScalar(obj, CFloat)) {
        return make_dtype<complex<float> >();
    } else if (PyArray_IsScalar(obj, CDouble)) {
        return make_dtype<complex<double> >();
    }

    throw std::runtime_error("could not deduce a pydnd dtype from the numpy scalar object");
}

template<class T>
static void py_decref_function(T* obj)
{
    Py_DECREF(obj);
}

ndarray pydnd::ndarray_from_numpy_array(PyArrayObject* obj)
{
    // Get the dtype of the array
    dtype d = pydnd::dtype_from_numpy_dtype(PyArray_DESCR(obj));
    // Get a shared pointer that tracks buffer ownership
    PyObject *base = PyArray_BASE(obj);
    dnd::shared_ptr<void> bufowner;
    if (base == NULL || (PyArray_FLAGS(obj)&NPY_ARRAY_UPDATEIFCOPY) != 0) {
        Py_INCREF(obj);
        bufowner = dnd::shared_ptr<PyArrayObject>(obj, py_decref_function<PyArrayObject>);
    } else {
        Py_INCREF(base);
        bufowner = dnd::shared_ptr<PyObject>(base, py_decref_function<PyObject>);
    }
    // Create the result ndarray
    return ndarray(new strided_array_expr_node(d, PyArray_NDIM(obj), 
                    PyArray_DIMS(obj), PyArray_STRIDES(obj), PyArray_DATA(obj), DND_MOVE(bufowner)));
}

dnd::ndarray pydnd::ndarray_from_numpy_scalar(PyObject* obj)
{
    if (PyArray_IsScalar(obj, Bool)) {
        return ndarray((bool)((PyBoolScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, Byte)) {
        return ndarray(((PyByteScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, UByte)) {
        return ndarray(((PyUByteScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, Short)) {
        return ndarray(((PyShortScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, UShort)) {
        return ndarray(((PyUShortScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, Int)) {
        return ndarray(((PyIntScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, UInt)) {
        return ndarray(((PyUIntScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, Long)) {
        return ndarray(((PyLongScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, ULong)) {
        return ndarray(((PyULongScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, LongLong)) {
        return ndarray(((PyLongLongScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, ULongLong)) {
        return ndarray(((PyULongLongScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, Float)) {
        return ndarray(((PyFloatScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, Double)) {
        return ndarray(((PyDoubleScalarObject *)obj)->obval);
    } else if (PyArray_IsScalar(obj, CFloat)) {
        npy_cfloat& val = ((PyCFloatScalarObject *)obj)->obval;
        return ndarray(complex<float>(val.real, val.imag));
    } else if (PyArray_IsScalar(obj, CDouble)) {
        npy_cdouble& val = ((PyCDoubleScalarObject *)obj)->obval;
        return ndarray(complex<double>(val.real, val.imag));
    }

    throw std::runtime_error("could not create a dnd::ndarray from the numpy scalar object");
}

char pydnd::numpy_kindchar_of(const dnd::dtype& d)
{
    switch (d.kind()) {
    case bool_kind:
        return 'b';
    case int_kind:
        return 'i';
    case uint_kind:
        return 'u';
    case real_kind:
        return 'f';
    case complex_kind:
        return 'c';
    default: {
        stringstream ss;
        ss << "dnd::dtype \"" << d << "\" does not have an equivalent numpy kind";
        throw runtime_error(ss.str());
        }
    }
}

static void free_array_interface(void *ptr, void *extra_ptr)
{
    PyArrayInterface* inter = (PyArrayInterface *)ptr;
    dnd::shared_ptr<void> *extra = (dnd::shared_ptr<void> *)extra_ptr;
    delete[] inter->shape;
    delete[] inter->strides;
    delete inter;
    delete extra;
}

PyObject* pydnd::ndarray_as_numpy_struct_capsule(const dnd::ndarray& n)
{
    if (n.get_expr_tree()->get_node_type() != strided_array_node_type) {
        throw runtime_error("cannot convert a dnd::ndarray that isn't a strided array into a numpy array");
    }

    PyArrayInterface inter;
    memset(&inter, 0, sizeof(inter));

    inter.two = 2;
    inter.nd = n.get_ndim();
    inter.typekind = numpy_kindchar_of(n.get_dtype());
    inter.itemsize = (int)n.get_dtype().itemsize();
    // TODO: When read-write access control is added, this must be modified
    inter.flags = NPY_ARRAY_NOTSWAPPED | NPY_ARRAY_ALIGNED | NPY_ARRAY_WRITEABLE;
    inter.data = n.get_originptr();
    inter.strides = new intptr_t[n.get_ndim()];
    inter.shape = new intptr_t[n.get_ndim()];

    memcpy(inter.strides, n.get_strides(), n.get_ndim() * sizeof(intptr_t));
    memcpy(inter.shape, n.get_shape(), n.get_ndim() * sizeof(intptr_t));

    // TODO: Check for Python 3, use PyCapsule there
    return PyCObject_FromVoidPtrAndDesc(new PyArrayInterface(inter), new dnd::shared_ptr<void>(n.get_buffer_owner()), free_array_interface);
}

#endif // DND_NUMPY_INTEROP