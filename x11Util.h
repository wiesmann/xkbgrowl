/*
 *  x11Util.h
 *  xkbgrowl
 *
 *  Created by Matthias Wiesmann on 16.08.09.
 *  Copyright 2009 Matthias Wiesmann. All rights reserved.
 *  The source code of this program is licensed under the Apache 2.0 license.
 *  For more information, see http://www.apache.org/licenses/LICENSE-2.0
 */

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Wrapper interface that holds the various elements of a X11 bell event.
// ─────────────────────────────────────────────────────────────────────────────
class BellEvent {
 public:
  BellEvent();
  virtual ~BellEvent();
  virtual std::string name() const = 0;        // name of the event
  virtual std::string windowName() const = 0;  // window name or empty
  virtual std::string iconPath() const = 0;    // path to icon file or empty
  virtual std::string iconMaskPath() const = 0;// path to mask file or empty
  virtual std::string hostName() const = 0;    // hostname where event occured
  virtual int pitch() const = 0;               // beep pitch
  virtual int percent() const = 0;             // beep percentage -100 - 100
  virtual int duration() const = 0;            // beep duration
  virtual int bellClass() const = 0;           // beep class
  virtual int bellId() const = 0;              // beep id
  virtual bool eventOnly() const = 0;          // is this only an event
};

// ─────────────────────────────────────────────────────────────────────────────
// Wrapper interface for the X11 subsystem
// We hide most of the X11 internals to avoid names conflicts with the Mac OS
// side (in particular Quickdraw).
// ─────────────────────────────────────────────────────────────────────────────

class X11DisplayData {
 protected:
  const std::string displayName_;
  const std::string programName_;
  explicit X11DisplayData(const std::string& programName, const std::string& displayName);
 public:
  /// Factory method, constructs a concrete instance, caller owns the instance.
  static X11DisplayData* GetDisplayData(const std::string& programName, const std::string& displayName);
  virtual ~X11DisplayData();
  virtual BellEvent* NextBellEvent() = 0;            // block until next event, event is owned by caller.
  virtual void SendBellEvent(const std::string& name) = 0;  // send a new bell event
};

