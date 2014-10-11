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

int handleError(Display* display, XErrorEvent* error) {
  char buffer[256];
  XGetErrorText(display, error->error_code, &buffer[0], sizeof(buffer));
  fprintf(stderr, "X11 error: %s\n", buffer);
  return 0;
}

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
  XSetErrorHandler(handleError);
}

void X11DisplayDataImpl::SendBellEvent(const std::string& name) {
  const Atom bellname_ = XInternAtom(display(), name.c_str(), False);
  XkbBellEvent(display(), None, 100, bellname_);
}

X11DisplayDataImpl::~X11DisplayDataImpl() {
  XCloseDisplay(display_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Image proxy implementation that holds two XImages
// ─────────────────────────────────────────────────────────────────────────────

class XImageProxy : public ImageProxy {
public:
  XImageProxy(XImage* pixmap, XImage* mask, Display* display, Colormap color_map);
  ~XImageProxy();
  
  void provideARGB(int x, int y, int width, int height, void* data) const;
private:
  unsigned char* providePixel(int x, int y, unsigned char* p) const;

  XImage* const pixmap_;  // Icon pixmap, owned.
  XImage* const mask_;    // Mask pixmap, owned.
  Display* const display_;  // Display, not owned.
  Colormap color_map_;      // Colormap
};

XImageProxy::XImageProxy(XImage* pixmap, XImage* mask, Display* display, Colormap color_map)
: ImageProxy(pixmap->width, pixmap->height), pixmap_(pixmap), mask_(mask), display_(display_), color_map_(color_map) {
  assert(display != nullptr);
  assert(pixmap != nullptr);
}

XImageProxy::~XImageProxy() {
  if (pixmap_ != nullptr) {
    XDestroyImage(pixmap_);
  }
  if (mask_ != nullptr) {
    XDestroyImage(mask_);
  }
}

// The images defined in the XImages for X logos should only be one bit.
// Strangely enough, xterm provides 8 palette colour data.
Status FakeXQueryColor(Display* display, Colormap color_map, XColor* color) {
  if (color->pixel) {
    color->red = 0x0000;
    color->green = 0x0000;
    color->blue = 0x0000;
  } else {
    color->red = 0xffff;
    color->green = 0xffff;
    color->blue = 0xffff;
  }
  return 1;
}

unsigned char* XImageProxy::providePixel(int x, int y, unsigned char* p) const {
  assert(x < width_);
  assert(y < height_);
  XColor color;
  color.pixel = XGetPixel(pixmap_, x, y);
  const Status color_status = FakeXQueryColor(display_, color_map_, &color);
  if (!color_status) {
    fprintf(stderr, "Could not lookup pixel %d %d: %d", x, y, color_status);
  }
  // alpha
  if (mask_ != nullptr) {
    const unsigned long alpha_v = XGetPixel(mask_, x, y);
    if (alpha_v) {
      *p++ = 0xff;
    } else {
      *p++ = 0x00;
    }
  } else {
    *p++ = 0xff;
  }
  *p++ = (color.red >> 8);
  *p++ = (color.green >> 8);
  *p++ = (color.blue >> 8);
  return p;
}

void XImageProxy::provideARGB(int x, int y, int width, int height, void* data) const {
  unsigned char* p = static_cast<unsigned char*>(data);
  for (int y_index = y; y_index < y + height; ++y_index) {
    for (int x_index = x; x_index < x + width; ++x_index) {
      p = providePixel(x_index, y_index, p);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Image proxy implementation that holds raw rgb bytes
// ─────────────────────────────────────────────────────────────────────────────

class RawImageProxy : public ImageProxy {
public:
  RawImageProxy(int width, int height, const unsigned char* data);
  ~RawImageProxy();
  
  void provideARGB(int x, int y, int width, int height, void* const data) const;
private:
  std::unique_ptr<unsigned char[]> pixels_;
};

RawImageProxy::RawImageProxy(int width, int height, const unsigned char* const data)
:ImageProxy(width, height) {
  const size_t num_bytes = width * height * 4;
  pixels_.reset(new unsigned char[num_bytes]);
  memcpy(pixels_.get(), data, num_bytes);
}

RawImageProxy::~RawImageProxy() {}

void RawImageProxy::provideARGB(int x, int y, int width, int height, void* const data) const {
  unsigned char* dest = static_cast<unsigned char *>(data);
  for(int yd = 0; yd < height; ++yd) {
    for(int xd = 0; xd < width; ++xd) {
      const int p = (((yd + y) * width) + (xd + x)) * 4;
      for (int c = 0; c < 4; ++c) {
        *dest = pixels_[p + c];
        ++dest;
      }
    }
  }
}


// ─────────────────────────────────────────────────────────────────────────────
// Concrete implementation of the BellEvent class
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
  std::unique_ptr<ImageProxy> image_proxy_;
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

XImage* GetImage(Display* display, Drawable drawable) {
  Window root;
  int x, y;
  unsigned int width, height;
  unsigned int border, depth;
  const Status status = XGetGeometry(display, drawable,
                                     &root, &x, &y,  &width, &height, &border, &depth);
  if (status) {
    // XGetImage does not work for windows that are not somehow mapped.
    XImage* image = XGetImage(display, drawable, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) {
      fprintf(stderr, "XGetImage failed for drawable %lx\n", drawable);
    }
    return image;
  }
  return nullptr;
}

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
  // First try to get the _NET_WM_ICON
  unsigned long nitems;
  unsigned long bytesafter;
  unsigned char* result;
  int format;
  Atom type;
  const Atom net_wm_icon = XInternAtom(display(), "_NET_WM_ICON", False);
  XGetWindowProperty(display(), window, net_wm_icon, 0, 1, 0, XA_CARDINAL,
                     &type, &format, &nitems, &bytesafter,  &result);
  if (result) {
    const int icon_width = *reinterpret_cast<int*>(result);
    XFree(result);
    XGetWindowProperty(display(), window, net_wm_icon, 1, 1, 0, XA_CARDINAL,
                       &type, &format,  &nitems, &bytesafter, &result);
    if (result) {
      const int icon_height = *reinterpret_cast<int*>(result);
      XFree(result);
      const int icon_size = icon_width * icon_height;
      XGetWindowProperty(display(), window, net_wm_icon, 2, icon_size, 0, XA_CARDINAL,
                         &type, &format,  &nitems, &bytesafter, &result);
      if (result) {
        image_proxy_.reset(new RawImageProxy(icon_width, icon_height, result));
        XFree(result);
      }
    }
    return;
  }
  // Fallback to old-school X11 icons.
  wmHints_ = XGetWMHints(display(), window);
  Colormap color_map = DefaultColormap(display(), DefaultScreen(display()));
  if (wmHints_) {
    // Icon Window
    if (wmHints_->flags & IconWindowHint) {
      XImage* const win_image = GetImage(display(), wmHints_->icon_window);
      if (win_image) {
        image_proxy_.reset(new XImageProxy(win_image, nullptr, display(), color_map));
        return;
      } else {
        fprintf(stderr, "Failed to build XImage for window icon.\n");
      }
    }
    // Icon
    if (wmHints_->flags & IconPixmapHint) {
      XImage* const pixmap = GetImage(display(), wmHints_->icon_pixmap);
      if (pixmap) {
        XImage* mask = nullptr;
        if (wmHints_->flags & IconMaskHint) {
          mask = GetImage(display(), wmHints_->icon_mask);
        }
        image_proxy_.reset(new XImageProxy(pixmap, mask, display(), color_map));
      }
    }
  } // Has wmHints_
} // GetAttributesFromWindow

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

