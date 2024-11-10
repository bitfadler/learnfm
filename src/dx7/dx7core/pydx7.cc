
#include <Python.h>

#include <cstdlib>
#include <math.h>
#include "synth.h"
#include "module.h"
#include "aligned_buf.h"
#include "freqlut.h"
#include "wavout.h"
#include "sawtooth.h"
#include "sin.h"
#include "exp2.h"
#include "log2.h"
#include "resofilter.h"
#include "fm_core.h"
#include "fm_op_kernel.h"
#include "env.h"
#include "patch.h"
#include "controllers.h"
#include "dx7note.h"

#include <iostream>
#include <filesystem>

static char *unpacked_patches = 0;
static PyObject *dx7_error = 0;

void write_data(const int32_t *buf_in, short * buf_out, unsigned int * pos, int n) {
  int32_t delta = 0x100;
  for (int i = 0; i < n; i++) {
    int32_t val = buf_in[i];
    int clip_val = val < -(1 << 24) ? 0x8000 : (val >= (1 << 24) ? 0x7fff :
                                                (val + delta) >> 9);
    delta = (delta + val) & 0x1ff;
    buf_out[*pos + i] = clip_val; 
  }
  *pos = *pos + n;
}

// Render but with 156 bytes of patch data
short * render_patchdata(const char* patch_data, unsigned char midinote, unsigned char velocity, unsigned int samples, unsigned int keyup) {
  if( patch_data == 0 )
    return 0;

  Dx7Note note;
  short * out = (short *) malloc(sizeof(short) * samples +N);
  unsigned int out_ptr = 0;
  note.init(patch_data, midinote, velocity);
  Controllers controllers;
  controllers.values_[kControllerPitch] = 0x2000;
  int32_t buf[N];

  for (unsigned int i = 0; i < samples; i += N) {
    for (unsigned int j = 0; j < N; j++) {
      buf[j] = 0;
    }
    if (i >= keyup) {
      note.keyup();
    }
    note.compute(buf, 0, 0, &controllers);
    for (unsigned int j = 0; j < N; j++) {
      buf[j] >>= 2;
    }
    write_data(buf, out, &out_ptr, N);
  }
  return out;
}

void init_synth(void) {
  double sample_rate = 44100.0;
  Freqlut::init(sample_rate);
  Sawtooth::init(sample_rate);
  Sin::init();
  Exp2::init();
  Log2::init();
}

static PyObject * render_patchdata_wrapper(PyObject *self, PyObject *args) {
  /* Default values. */
  int arg2 = 50; // midi note
  int arg3 = 70; // velocity
  int arg4 = 44100; // samples
  int arg5 = 22050; // keyup sample
  
  const char * patch_save;
  Py_ssize_t count;
  if (! PyArg_ParseTuple(args, "s#iiii", &patch_save, &count, &arg2, &arg3, &arg4, &arg5)) {
    return NULL;
  }

  short * result = render_patchdata(patch_save, arg2, arg3, arg4, arg5);
  if( result == 0 )
    return 0;

  // Create a python list of ints (they are signed shorts that come back)
  PyObject* ret = PyList_New(arg4); // arg4 is samples
  for (int i = 0; i < arg4; ++i) {
    PyObject* python_int = Py_BuildValue("i", result[i]);
    PyList_SetItem(ret, i, python_int);
  }

  free(result);
  return ret;
}


static PyObject * render_wrapper(PyObject *self, PyObject *args) {
  /* Default values. */
  int arg1 = 8; // patch #
  int arg2 = 50; // midi note
  int arg3 = 70; // velocity
  int arg4 = 44100; // samples
  int arg5 = 22050; // keyup sample

  if (! PyArg_ParseTuple(args, "iiiii", &arg1, &arg2, &arg3, &arg4, &arg5)) {
    return NULL;
  }

  const char* patch_data = &unpacked_patches[arg1 * 156];
  short* result = render_patchdata(patch_data, arg2, arg3, arg4, arg5);
  if( result == 0 )
    return 0;

  // Create a python list of ints (they are signed shorts that come back)
  PyObject* ret = PyList_New(arg4); // arg4 is samples
  for (int i = 0; i < arg4; ++i) {
    PyObject* python_int = Py_BuildValue("i", result[i]);
    PyList_SetItem(ret, i, python_int);
  }

  free(result);
  return ret;
}

// return one patch unpacked for sysex
static PyObject * unpack_wrapper(PyObject *self, PyObject *args) {
  int arg1 = 8; // patch #
  if (! PyArg_ParseTuple(args, "i", &arg1)) {
    return NULL;
  }
  PyObject* ret = PyList_New(155); 
  for (int i = 0; i < 155; i++) {
    PyObject* python_int = Py_BuildValue("i", unpacked_patches[arg1*156 + i]);
    PyList_SetItem(ret, i, python_int);    
  }
  return ret;
}

//----------------------------------------------------------------------------------------

/// @brief Build the filename of the database file by using __file__.
/// @param module Pointer to existing python module instance
/// @param filename Const reference to the filename without any path info
/// @return String containing the fullpath to the database file
std::string Dx7BuildDatabaseFilename(PyObject *module, const std::string& filename) {
  PyObject *string = PyObject_GetAttrString(module, "__file__");
  if( string == nullptr)
    return filename;

  std::string path( PyUnicode_AsUTF8(string) );
  size_t found = path.find_last_of("/\\");
  if( found == std::string::npos )
    return filename;

  std::string fullpath( path.substr(0,found) );
  fullpath += "/dx7/bin/";
  fullpath += filename;
  
  return fullpath;
}

/// @brief Execute DX7 module initialization as part of a multi-stage process. Here we have access to the __file__ variable! 
/// @param module Pointer to the model instance
/// @return 0 if successful, -1 otherwise. Set an exception if returning -1.
int Dx7ExecuteModule(PyObject *module) {
  
  
  dx7_error = PyErr_NewException("dx7.error", NULL, NULL);
  if (PyModule_AddObjectRef(module, "error", dx7_error) < 0) {
    Py_CLEAR(dx7_error);
    Py_DECREF(module);
    return -1;
  }

  if (!PyObject_HasAttrString(module, "__file__")) {
    PyErr_SetString(dx7_error, "Cannot derive database path from __file__!");
    return -1;
  }

  std::string database_file = Dx7BuildDatabaseFilename(module, "compact.bin");

  FILE *f = fopen(database_file.c_str(),"rb");
  if( f == nullptr ) {
    PyErr_SetString(dx7_error, "Patch database not found!");
    return -1;
  }

  // See how many voices are in the file
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  char * all_patches = (char*)malloc(fsize);
  fseek(f,0,SEEK_SET);
  // Load them all in
  fread(all_patches, 1, fsize, f);
  fclose(f);

  const int patches = fsize / 128;
  // Patches have to be unpacked to go from 128 bytes to 156 bytes via DX7 spec
  unpacked_patches = (char*) malloc(patches*156);
  for(int i=0;i<patches;i++) {
    UnpackPatch(all_patches + (i*128), &unpacked_patches[i*156] );
  }
  free(all_patches);

  init_synth();

  return 0;
}

// ============================================================================
// Python module structs and main entry code

static PyMethodDef dx7_methods[] = {
 {"render", render_wrapper, METH_VARARGS, "Render audio"},
 {"render_patchdata", render_patchdata_wrapper, METH_VARARGS, "Render audio via patch data"},
 {"unpack", unpack_wrapper, METH_VARARGS, "Unpack patch"},
 { NULL, NULL, 0, NULL }
};

static struct PyModuleDef_Slot dx7_moduleslots[] = {
    {Py_mod_exec, (void*) Dx7ExecuteModule},
    {0, NULL}
};

static struct PyModuleDef dx7_module_definition =
{
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "dx7",        /* name of module */
    .m_doc = nullptr,            /* module documentation, may be NULL */
    .m_size = 0,           /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    .m_methods = dx7_methods,
    .m_slots = dx7_moduleslots,
};

// This is the entry point for the multi-stage initialization!
extern "C" {

PyMODINIT_FUNC PyInit_dx7(void)
{
  return PyModuleDef_Init(&dx7_module_definition); 
}

}
