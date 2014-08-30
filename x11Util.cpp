/*
 *  x11Util.cpp
 *  xkbgrowl
 *
 *  Created by Matthias Wiesmann on 16.08.09.
 *  Copyright 2009 Matthias Wiesmann. All rights reserved.
 *  The source code of this program is licensed under the Apache 2.0 license.
 *  For more information, see http://www.apache.org/licenses/LICENSE-2.0
 */


#include "x11Util.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBfile.h>
#include <X11/extensions/XKBbells.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

// ─────────────────────────────────────────────────────────────────────────────
// Error message formats
// ─────────────────────────────────────────────────────────────────────────────

const char* const kWrongVersionFormat = "%s was compiled with XKB version %d.%02d found %d.%02d in %s\n";
const char* const kConnectionRefusedFormat = "Could not connect to display %s.\n";
const char* const kNonXkbServerFormat = "X11 Server %s does not support XKB.\n";
const char* const kUnknownErrorFormat = "Unknown error %d while opening display %s.\n";
const char* const kSelectEventErrorFormat = "Could not get XKB bell events for display %s.\n";
const char* const kWindowNameError = "Could not retrieve name for window %lx.\n";
const char* const kWriteBitmapError = "Could not write bitmap %s: %d\n";
const char* const kUnlinkError = "Could not delete file %s\n";
const char* const kUnknownClientNameError = "Could not get client name for window %lx.\n";
const char* const kEmptyString = "";
const char* const kPixMapName = "pixmap";
const char* const kPixMapMaskName = "mask";

// ─────────────────────────────────────────────────────────────────────────────
// Various constants
// ─────────────────────────────────────────────────────────────────────────────

const size_t kBufferSize = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Conversion function
// ─────────────────────────────────────────────────────────────────────────────

// Output a pixmap into a file.
// If the conversion is sucessful, this function returns a new string with the path of
// the file, both the string and the file should be deleted when not needed anymore.
// @return a path allocated on the stack, or null
char* OutputPixMap(Pixmap pixmap, Display* display, const char* const name, long serial) {
  Window root;
  int x, y; 
  unsigned int width, height;
  unsigned int border, depth;
  Status status = 0;
  status = XGetGeometry(display, pixmap, &root, &x, &y,  &width, &height, &border, &depth);
  if (!status) {
    return NULL;
  }
  char* path = new char[kBufferSize];
  snprintf(path, kBufferSize, "/tmp/%s_%lx.xpm", name, serial);
  status = XWriteBitmapFile(display, path, pixmap, width, height, -1 , -1);
  if (status != BitmapSuccess) {
    delete path;
    fprintf(stderr, kWriteBitmapError, path, status);
    return NULL;
  }
  return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// Abstract classes methods
// ─────────────────────────────────────────────────────────────────────────────

BellEvent::BellEvent() {}
BellEvent::~BellEvent() {}

X11DisplayData::X11DisplayData(const char* programName,
                               const char* displayName) : _programName(programName), _displayName(displayName) {}

X11DisplayData::~X11DisplayData() {}

// ─────────────────────────────────────────────────────────────────────────────
// Concrete implementation of the X11 display wrapper.
// ─────────────────────────────────────────────────────────────────────────────
class X11DisplayDataImpl : public X11DisplayData {
  
private:
  X11DisplayDataImpl& operator=(const X11DisplayDataImpl& other) {return *this;}
protected:
  Display* _display;  // not owned
  int _xkbOpcode;
  int _xkbEventCode;
public:
  X11DisplayDataImpl(const char* programName, const char* displayName);
  virtual ~X11DisplayDataImpl();
  virtual BellEvent* NextBellEvent();
  Display* display() { return _display; }
  virtual void SendBellEvent(const char* name);
};


X11DisplayData* X11DisplayData::GetDisplayData(const char* programName,
                                               const char* displayName) {
  return new X11DisplayDataImpl(programName, displayName);
}

// Struct to store X11 version stuff.
struct X11Version {
  int major;
  int minor;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constructor for the concrete implementation
// Most of the code is for error handling
// ─────────────────────────────────────────────────────────────────────────────
X11DisplayDataImpl::X11DisplayDataImpl(
                                       const char* programName,
                                       const char* displayName)
: X11DisplayData(programName, displayName), _display(NULL), _xkbOpcode(0), _xkbEventCode(0) {
  X11Version version = { XkbMajorVersion, XkbMinorVersion };
  int error = 0;
  _display = XkbOpenDisplay(const_cast<char *>(displayName), &_xkbEventCode,
                            NULL, &version.major, &version.minor, &error);
  if (_display == NULL) {
    switch (error) {
      case XkbOD_BadLibraryVersion:
        fprintf(stderr, kWrongVersionFormat, programName, XkbMajorVersion,
                XkbMinorVersion, version.major, version.minor, "library");
        exit(EX_CONFIG);
      case XkbOD_BadServerVersion:
        fprintf(stderr, kWrongVersionFormat, programName, XkbMajorVersion,
                XkbMinorVersion, version.major, version.minor, displayName);
        exit(EX_CONFIG);
      case XkbOD_ConnectionRefused:
        fprintf(stderr, kConnectionRefusedFormat, displayName);
        exit(EX_UNAVAILABLE);
      case XkbOD_NonXkbServer:
        fprintf(stderr, kNonXkbServerFormat, displayName);
        exit(EX_UNAVAILABLE);
      default:
        fprintf(stderr, kUnknownErrorFormat, error, displayName);
        exit(EX_SOFTWARE);
    } // switch
  }
  int eventMask = XkbBellNotifyMask;
  if (!XkbSelectEvents(_display, XkbUseCoreKbd, eventMask, eventMask)) {
    fprintf(stderr, kSelectEventErrorFormat, displayName);
    exit(EX_SOFTWARE);
  }
}

void X11DisplayDataImpl::SendBellEvent(const char* name) {
  Atom bell_name = XInternAtom(display(), name, False);
  XkbBellEvent(display(), None, 100, bell_name);
}

X11DisplayDataImpl::~X11DisplayDataImpl() {
  XCloseDisplay(_display);
}

// ─────────────────────────────────────────────────────────────────────────────
// Concrete implementation of the BellEvent class
// TODO: figure out a way to avoid writing out x11 icons to files and then
// read them back
// ─────────────────────────────────────────────────────────────────────────────
class BellEventImpl : public BellEvent {
public:
  BellEventImpl(X11DisplayDataImpl* data);
  virtual  ~BellEventImpl();
  virtual const char* name() const;
  virtual const char* windowName() const;
  virtual int pitch() const;
  virtual int percent() const;
  virtual int duration() const;
  virtual int bellClass() const;
  virtual int bellId() const;
  virtual bool eventOnly() const;
  virtual const char* iconPath() const;
  virtual const char* iconMaskPath() const;
  virtual const char* hostName() const;
protected:
  X11DisplayDataImpl* _data;
  XkbEvent _event;
  char* _name; 
  char* _windowName;
  char* _iconPath;
  char* _iconMaskPath;
  XTextProperty _hostName;
  XWMHints* _wmHints;
  inline Display* display();
  inline Window window();
  void GetAttributesFromWindow(Window window);
};

// Constructor, gets the event from the display
BellEventImpl::BellEventImpl(X11DisplayDataImpl* data) : 
    _data(data), _event(), _name(NULL), _windowName(NULL), _iconPath(NULL),_iconMaskPath(NULL),  _wmHints(NULL) {
  XNextEvent(display(), &_event.core);
  if (_event.bell.name) {
    _name = XGetAtomName(_data->display(), _event.bell.name);
  } 
  if (window()) {
    GetAttributesFromWindow(window());
  } else {
    const int screen = DefaultScreen(display());
    const Window root = RootWindow(display(), screen);
    GetAttributesFromWindow(root);
  }  
}

BellEventImpl::~BellEventImpl() {
  if (_name) {
    XFree(_name);
    _name = NULL;
  }
  if (_windowName) {
    XFree(_windowName);
    _windowName = NULL;
  }
  if (_wmHints) {
    XFree(_wmHints);
    _wmHints = NULL;
  }
  if (_hostName.value) {
    XFree(_hostName.value);
  }
  if (_iconPath) {
    const int status = unlink(_iconPath);
    if (status != 0) {
      char buffer[kBufferSize];
      snprintf(buffer, sizeof(buffer), kUnlinkError, _iconPath);
      perror(buffer);
    }
    delete _iconPath;
    _iconPath = NULL;
  }
  if (_iconMaskPath) {
    const int status = unlink(_iconMaskPath);
    if (status != 0) {
      char buffer[kBufferSize];
      snprintf(buffer, sizeof(buffer), kUnlinkError, _iconMaskPath);
      perror(buffer);
    }
    delete _iconMaskPath;
    _iconMaskPath = NULL;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract information from a Window, we extract the following:
// • Window name
// • Window icon
// ─────────────────────────────────────────────────────────────────────────────

void BellEventImpl::GetAttributesFromWindow(Window window) {
  assert(window);
  const Status nameStatus = XFetchName(display(), window, &_windowName);
  if (nameStatus == 0) {
    fprintf(stderr, kWindowNameError, window);
  }
  const Status hostStatus = XGetWMClientMachine(display(), window, &_hostName);
  if (hostStatus == 0) {
    fprintf(stderr, kUnknownClientNameError, window);
  }
  _wmHints = XGetWMHints(display(), window);
  if (_wmHints) {
    if (_wmHints->flags & IconPixmapHint) {
      _iconPath = OutputPixMap(_wmHints->icon_pixmap, display(), kPixMapName, _event.bell.serial);
    }
    if (_wmHints->flags & IconMaskHint) {
      _iconMaskPath = OutputPixMap(_wmHints->icon_mask, display(), kPixMapMaskName, _event.bell.serial);
    }
  } // Has _wmHints
}

Display* BellEventImpl::display() {
  return _data->display();
}

Window BellEventImpl::window() {
  return _event.bell.window;
}

const char* BellEventImpl::iconPath() const {
  return _iconPath;
}

const char* BellEventImpl::iconMaskPath() const {
  return _iconMaskPath;
}

// Name is an X11 atom, and therefore in iso-latin encoding
const char* BellEventImpl::name() const {
  if (_name) {
    return _name;
  } else {
    return kEmptyString;
  }
}

const char* BellEventImpl::windowName() const {
  return _windowName;
}

const char* BellEventImpl::hostName() const {
  return reinterpret_cast<const char*>(_hostName.value);
}

int BellEventImpl::pitch() const {
  return _event.bell.pitch;
}

int BellEventImpl::percent() const {
  return _event.bell.percent;
}

int BellEventImpl::duration() const {
  return _event.bell.duration;
}

int BellEventImpl::bellClass() const {
  return _event.bell.bell_class;
}

int BellEventImpl::bellId() const {
  return _event.bell.bell_id;
}

bool BellEventImpl::eventOnly() const {
  return _event.bell.event_only;
}

BellEvent* X11DisplayDataImpl::NextBellEvent() {
  return new BellEventImpl(this);
}

