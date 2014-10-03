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

const char kWrongVersionFormat[] = "%s was compiled with XKB version %d.%02d found %d.%02d in %s\n";
const char kConnectionRefusedFormat[] = "Could not connect to display %s.\n";
const char kNonXkbServerFormat[] = "X11 Server %s does not support XKB.\n";
const char kUnknownErrorFormat[] = "Unknown error %d while opening display %s.\n";
const char kSelectEventErrorFormat[] = "Could not get XKB bell events for display %s.\n";
const char kWindowNameError[] = "Could not retrieve name for window %lx.\n";
const char kWriteBitmapError[] = "Could not write bitmap %s: %d\n";
const char kUnlinkError[] = "Could not delete file %s\n";
const char kUnknownClientNameError[] = "Could not get client name for window %lx.\n";
const char kEmptyString[] = "";
const char kPixMapName[] = "pixmap";
const char kPixMapMaskName[] = "mask";

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
// @return a path allocated on the stack, or nullptr
// Avoid the round-trip to the file-system and also forced convesion to black/white.
std::string OutputPixMap(Pixmap pixmap, Display* display, const std::string& name, long serial) {
  Window root;
  int x, y; 
  unsigned int width, height;
  unsigned int border, depth;
  Status status = 0;
  status = XGetGeometry(display, pixmap, &root, &x, &y,  &width, &height, &border, &depth);
  if (!status) {
    return std::string();
  }
  char buffer[kBufferSize];
  snprintf(buffer, kBufferSize, "/tmp/%s_%06lx.xpm", name.c_str(), serial);
  status = XWriteBitmapFile(display, buffer, pixmap, width, height, -1 , -1);
  if (status != BitmapSuccess) {
    fprintf(stderr, kWriteBitmapError, buffer, status);
    return std::string();
  }
  return buffer;
}

// ─────────────────────────────────────────────────────────────────────────────
// Abstract classes methods
// ─────────────────────────────────────────────────────────────────────────────

BellEvent::BellEvent() {}
BellEvent::~BellEvent() {}

X11DisplayData::X11DisplayData(const std::string& programName,
                               const std::string& displayName) : programName_(programName), displayName_(displayName) {}

X11DisplayData::~X11DisplayData() {}

// ─────────────────────────────────────────────────────────────────────────────
// Concrete implementation of the X11 display wrapper.
// ─────────────────────────────────────────────────────────────────────────────
class X11DisplayDataImpl : public X11DisplayData {
  
private:
  X11DisplayDataImpl& operator=(const X11DisplayDataImpl& other) {return *this;}
protected:
  Display* display_;  // not owned
  int xkbOpcode_;
  int xkbEventCode_;
public:
  X11DisplayDataImpl(const std::string& programName, const std::string& displayName);
  virtual ~X11DisplayDataImpl();
  virtual BellEvent* NextBellEvent();
  Display* display() { return display_; }
  virtual void SendBellEvent(const std::string& name);
};


X11DisplayData* X11DisplayData::GetDisplayData(const std::string& programName,
                                               const std::string& displayName) {
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
                                       const std::string& programName,
                                       const std::string& displayName)
: X11DisplayData(programName, displayName), display_(nullptr), xkbOpcode_(0), xkbEventCode_(0) {
  X11Version version = { XkbMajorVersion, XkbMinorVersion };
  int error = 0;
  display_ = XkbOpenDisplay(const_cast<char *>(displayName.c_str()), &xkbEventCode_,
                            nullptr, &version.major, &version.minor, &error);
  if (display_ == nullptr) {
    switch (error) {
      case XkbOD_BadLibraryVersion:
        fprintf(stderr, kWrongVersionFormat, programName.c_str(), XkbMajorVersion,
                XkbMinorVersion, version.major, version.minor, "library");
        exit(EX_CONFIG);
      case XkbOD_BadServerVersion:
        fprintf(stderr, kWrongVersionFormat, programName.c_str(), XkbMajorVersion,
                XkbMinorVersion, version.major, version.minor, displayName.c_str());
        exit(EX_CONFIG);
      case XkbOD_ConnectionRefused:
        fprintf(stderr, kConnectionRefusedFormat, displayName.c_str());
        exit(EX_UNAVAILABLE);
      case XkbOD_NonXkbServer:
        fprintf(stderr, kNonXkbServerFormat, displayName.c_str());
        exit(EX_UNAVAILABLE);
      default:
        fprintf(stderr, kUnknownErrorFormat, error, displayName.c_str());
        exit(EX_SOFTWARE);
    } // switch
  }
  int eventMask = XkbBellNotifyMask;
  if (!XkbSelectEvents(display_, XkbUseCoreKbd, eventMask, eventMask)) {
    fprintf(stderr, kSelectEventErrorFormat, displayName.c_str());
    exit(EX_SOFTWARE);
  }
}

void X11DisplayDataImpl::SendBellEvent(const std::string& name) {
  Atom bellname_ = XInternAtom(display(), name.c_str(), False);
  XkbBellEvent(display(), None, 100, bellname_);
}

X11DisplayDataImpl::~X11DisplayDataImpl() {
  XCloseDisplay(display_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Concrete implementation of the BellEvent class
// TODO: figure out a way to avoid writing out x11 icons to files and then
// read them back
// ─────────────────────────────────────────────────────────────────────────────
class BellEventImpl : public BellEvent {
public:
  BellEventImpl(X11DisplayDataImpl* data);
  virtual ~BellEventImpl();
  virtual std::string name() const;
  virtual std::string windowName() const;
  virtual int pitch() const;
  virtual int percent() const;
  virtual int duration() const;
  virtual int bellClass() const;
  virtual int bellId() const;
  virtual bool eventOnly() const;
  virtual std::string iconPath() const;
  virtual std::string iconMaskPath() const;
  virtual std::string hostName() const;
protected:
  X11DisplayDataImpl* data_;
  XkbEvent event_;
  char* name_; 
  char* windowName_;
  std::string iconPath_;
  std::string iconMaskPath_;
  XTextProperty hostName_;
  XWMHints* wmHints_;
  inline Display* display();
  inline Window window();
  void GetAttributesFromWindow(Window window);
};

// Constructor, gets the event from the display
BellEventImpl::BellEventImpl(X11DisplayDataImpl* data) : 
    data_(data), event_(), name_(nullptr), windowName_(nullptr),
    iconPath_(), iconMaskPath_(),  wmHints_(nullptr) {
  XNextEvent(display(), &event_.core);
  if (event_.bell.name) {
    // XGetAtomName -> must be freed with XFree().
    name_ = XGetAtomName(data_->display(), event_.bell.name);
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
  if (name_) {
    XFree(name_);
    name_ = nullptr;
  }
  if (windowName_) {
    XFree(windowName_);
    windowName_ = nullptr;
  }
  if (wmHints_) {
    XFree(wmHints_);
    wmHints_ = nullptr;
  }
  if (hostName_.value) {
    XFree(hostName_.value);
  }
  if (!iconPath_.empty()) {
    const int status = unlink(iconPath_.c_str());
    if (status != 0) {
      char buffer[kBufferSize];
      snprintf(buffer, sizeof(buffer), kUnlinkError, iconPath_.c_str());
      perror(buffer);
    }
    iconPath_.clear();
  }
  if (!iconPath_.empty()) {
    const int status = unlink(iconMaskPath_.c_str());
    if (status != 0) {
      char buffer[kBufferSize];
      snprintf(buffer, sizeof(buffer), kUnlinkError, iconMaskPath_.c_str());
      perror(buffer);
    }
    iconPath_.clear();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract information from a Window, we extract the following:
// • Window name
// • Window icon
// ─────────────────────────────────────────────────────────────────────────────

void BellEventImpl::GetAttributesFromWindow(Window window) {
  assert(window);
  const Status nameStatus = XFetchName(display(), window, &windowName_);
  if (nameStatus == 0) {
    fprintf(stderr, kWindowNameError, window);
  }
  const Status hostStatus = XGetWMClientMachine(display(), window, &hostName_);
  if (hostStatus == 0) {
    fprintf(stderr, kUnknownClientNameError, window);
  }
  wmHints_ = XGetWMHints(display(), window);
  if (wmHints_) {
    if (wmHints_->flags & IconPixmapHint) {
      iconPath_ = OutputPixMap(wmHints_->icon_pixmap, display(), kPixMapName, event_.bell.serial);
    }
    if (wmHints_->flags & IconMaskHint) {
      iconMaskPath_ = OutputPixMap(wmHints_->icon_mask, display(), kPixMapMaskName, event_.bell.serial);
    }
  } // Has wmHints_
}

Display* BellEventImpl::display() {
  return data_->display();
}

Window BellEventImpl::window() {
  return event_.bell.window;
}

std::string BellEventImpl::iconPath() const {
  return iconPath_;
}

std::string BellEventImpl::iconMaskPath() const {
  return iconMaskPath_;
}

// Name is an X11 atom, and therefore in iso-latin encoding
std::string BellEventImpl::name() const {
  if (name_) {
    return name_;
  } else {
    return kEmptyString;
  }
}

std::string BellEventImpl::windowName() const {
  if (windowName_) {
    return windowName_;
  }
  return kEmptyString;
}

std::string BellEventImpl::hostName() const {
  const char* const hostname = reinterpret_cast<const char*>(hostName_.value);
  if (hostname) {
    return hostname;
  }
  return kEmptyString;
}

int BellEventImpl::pitch() const {
  return event_.bell.pitch;
}

int BellEventImpl::percent() const {
  return event_.bell.percent;
}

int BellEventImpl::duration() const {
  return event_.bell.duration;
}

int BellEventImpl::bellClass() const {
  return event_.bell.bell_class;
}

int BellEventImpl::bellId() const {
  return event_.bell.bell_id;
}

bool BellEventImpl::eventOnly() const {
  return event_.bell.event_only;
}

BellEvent* X11DisplayDataImpl::NextBellEvent() {
  return new BellEventImpl(this);
}

