
#include <Python.h>
//#include <iostream>
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

char * unpacked_patches = 0;

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

// take in a patch #, a note # and a velocity and a sample length and a sample # to lift off a key?
short * render(unsigned short patch, unsigned char midinote, unsigned char velocity, unsigned int samples, unsigned int keyup) {  
  if( unpacked_patches == 0 )
    return 0;
  
  const char* patch_data = &unpacked_patches[patch * 156];
  return render_patchdata (patch_data, midinote, velocity, samples, keyup);
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
  int arg2, arg3, arg4, arg5;
  /* Default values. */
  arg2 = 50; // midi note
  arg3 = 70; // velocity
  arg4 = 44100; // samples
  arg5 = 22050; // keyup sample
  const char * patch_save;
  Py_ssize_t count;
  if (! PyArg_ParseTuple(args, "s#iiii", &patch_save, &count, &arg2, &arg3, &arg4, &arg5)) {
    return NULL;
  }

  short * result;
  result = render_patchdata(patch_save, arg2, arg3, arg4, arg5);
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
  int arg1, arg2, arg3, arg4, arg5;

  /* Default values. */
  arg1 = 8; // patch #
  arg2 = 50; // midi note
  arg3 = 70; // velocity
  arg4 = 44100; // samples
  arg5 = 22050; // keyup sample

  if (! PyArg_ParseTuple(args, "iiiii", &arg1, &arg2, &arg3, &arg4, &arg5)) {
    return NULL;
  }

  short * result;
  result = render(arg1, arg2, arg3, arg4, arg5);
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

static PyMethodDef DX7Methods[] = {
 {"render", render_wrapper, METH_VARARGS, "Render audio"},
 {"render_patchdata", render_patchdata_wrapper, METH_VARARGS, "Render audio via patch data"},
 {"unpack", unpack_wrapper, METH_VARARGS, "Unpack patch"},
 { NULL, NULL, 0, NULL }
};

static struct PyModuleDef dx7Def =
{
    PyModuleDef_HEAD_INIT,
    "dx7", /* name of module */
    "",          /* module documentation, may be NULL */
    -1,          /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
    DX7Methods
};

extern "C" {
PyMODINIT_FUNC PyInit_dx7(void)
{
  // TODO: this file should go into the package directory from setup.py, but that is a PITA 
  FILE *f = fopen("./bin/compact.bin","rb");
  if( f == nullptr ) {
    printf("dx7: compact.bin not found.\n" );
    return nullptr;
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

  // Have to call this out as we're in C extern 
  init_synth();
  free(all_patches);
  return PyModule_Create(&dx7Def);
}
}
