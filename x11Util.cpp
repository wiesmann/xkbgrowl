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

const char kUnknownClientNameError[] = "Could not get client name for window %lx.\n";
const char kEmptyString[] = "";


// ─────────────────────────────────────────────────────────────────────────────
// Various constants
// ─────────────────────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────────────────────
// Conversion function
// ─────────────────────────────────────────────────────────────────────────────


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
// Image proxy implementation
// ─────────────────────────────────────────────────────────────────────────────

class ImageProxyImpl : public ImageProxy {
public:
  ImageProxyImpl(XImage* pixmap, XImage* mask, Display* display);
  ~ImageProxyImpl();
  
  void provideARGB(int x, int y, int width, int height, void* data);
private:
  XImage* pixmap_;
  XImage* mask_;
  Display* display_;
};

ImageProxyImpl::ImageProxyImpl(XImage* pixmap, XImage* mask, Display* display)
: ImageProxy(pixmap->height, pixmap->width), pixmap_(pixmap), mask_(mask), display_(display_) {
  printf("pixmap with depth %d %d\n", pixmap->depth, pixmap->bits_per_pixel);
}

ImageProxyImpl::~ImageProxyImpl() {
  if (pixmap_ != nullptr) {
    XDestroyImage(pixmap_);
  }
  if (mask_ != nullptr) {
    XDestroyImage(mask_);
  }
}


void ImageProxyImpl::provideARGB(int x, int y, int width, int height, void* data) {
  unsigned char* p = static_cast<unsigned char*>(data);
  for (int y_index = y; y_index < y + height; ++y_index) {
    for (int x_index = x; x_index < x + width; ++x_index) {
      const unsigned long v = XGetPixel(pixmap_, x_index, y_index);
      printf("%06lx ", v);
      *p++ = 255;  // alpha
      if (v) {
        *p++ = 0x00;
        *p++ = 0x00;
        *p++ = 0x00;
      } else {
        *p++ = 0xff;
        *p++ = 0xff;
        *p++ = 0xff;
      }
    }
  }
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
  virtual std::string hostName() const;
  virtual ImageProxy* imageProxy();

protected:
  
  inline Display* display();
  inline Window window();
  void GetAttributesFromWindow(Window window);
  
  X11DisplayDataImpl* data_;
  XkbEvent event_;
  char* name_; 
  char* windowName_;
  XTextProperty hostName_;
  XWMHints* wmHints_;
  std::unique_ptr<ImageProxyImpl> image_proxy_;
};

// Constructor, gets the event from the display
BellEventImpl::BellEventImpl(X11DisplayDataImpl* data) : 
    data_(data), event_(), name_(nullptr), windowName_(nullptr),
    wmHints_(nullptr) {
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
      Window root;
      int x, y;
      unsigned int width, height;
      unsigned int border, depth;
      Status status = XGetGeometry(display(), wmHints_->icon_pixmap, &root, &x, &y,  &width, &height, &border, &depth);
      if (status) {
        XImage* pixmap = XGetImage(display(), wmHints_->icon_pixmap, 0,0, width, height, depth, ZPixmap);
        image_proxy_.reset(new ImageProxyImpl(pixmap, nullptr, display()));
      }
    }
  } // Has wmHints_
}

Display* BellEventImpl::display() {
  return data_->display();
}

Window BellEventImpl::window() {
  return event_.bell.window;
}

ImageProxy* BellEventImpl::imageProxy() {
  return image_proxy_.get();
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

