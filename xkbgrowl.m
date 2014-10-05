/*
 *  xkbgrowl.m
 *  xkbgrowl
 *
 *  Created by Matthias Wiesmann on 16.08.09.
 *  Copyright 2009 Matthias Wiesmann. All rights reserved.
 *  The source code of this program is licensed under the Apache 2.0 license.
 *  For more information, see http://www.apache.org/licenses/LICENSE-2.0
 *
 *  This program translates X11 keyboard notifications into Growl notifications.
 *
 *  Current features:
 *  - Notification type is translated in growl message text
 *  - If a X11 window is associated to the event
 *    - The program's name is prefixed to the text
 *    - The window's X11 window manager icon is retrieved and used as custom
 *      notification icon.
 *
 *
 *  To try this program:
 *  - start it from the command line in one terminal
 *  - in another terminal type xkbbell "Some text"
 *  The text "Some text" should display as a growl notification.
 *
 *  Caveats:
 *  - Growl should be installed and running.
 */

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CoreImage.h>
#import "IconProvider.h"
#include <sysexits.h>
#include <signal.h>
#include <getopt.h>
#include <sandbox.h>
#include <memory>

#include "x11Util.h"

const char kNoGrowlError[] = "Could not connect to Growl\n";
const char kGrowlVersionMessage[] = "Connected to Growl %s.\n";
const char kUsage[] = "X11 Keyboard bell to Growl notification bridge.\nUsage: %s [-display DISPLAY]\n";
const char kDisplayEnv[] = "DISPLAY";
const char kDisplayArg[] = "display";

// ─────────────────────────────────────────────────────────────────────────────
// Growl Notification interface.
// ─────────────────────────────────────────────────────────────────────────────

@protocol GrowlNotificationProtocol
- (oneway void) registerApplicationWithDictionary:(bycopy NSDictionary *)dict;
- (oneway void) postNotificationWithDictionary:(bycopy NSDictionary *)notification;
- (bycopy NSString *) growlVersion;
@end


// ─────────────────────────────────────────────────────────────────────────────
// Factory to get an interface to the local growl instance.
// ─────────────────────────────────────────────────────────────────────────────
id<GrowlNotificationProtocol> getGrowlProxy() {
  NSArray* eventNames = [NSArray arrayWithObjects: @"Bell", nil];
  NSArray* registerKeys = [NSArray arrayWithObjects:@"ApplicationName", @"AllNotifications", @"DefaultNotifications", nil];
  NSArray* registerObjects = [NSArray arrayWithObjects: @"XKB", eventNames, eventNames, nil];
  NSDictionary* registerInfo = [NSDictionary dictionaryWithObjects:registerObjects forKeys: registerKeys];
  NSConnection* connection = [NSConnection connectionWithRegisteredName:@"GrowlApplicationBridgePathway" host:nil];
  NSDistantObject* theProxy = [connection rootProxy];
  if (!theProxy) {
    fprintf(stderr, kNoGrowlError);
    exit(EX_UNAVAILABLE);
  }
  [theProxy setProtocolForProxy:@protocol(GrowlNotificationProtocol)];
  id<GrowlNotificationProtocol> growlProxy = (id)theProxy;
  NSString* version = [growlProxy growlVersion];
  fprintf(stdout, kGrowlVersionMessage, [version UTF8String]);
  [growlProxy registerApplicationWithDictionary:(NSDictionary *)registerInfo];
  return growlProxy;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get the default icon data for X11.app
// ─────────────────────────────────────────────────────────────────────────────
NSData* getX11IconData() {
  NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
  NSString* x11Path = [workspace fullPathForApplication:@"XQuartz"];
  NSImage* x11Icon = [workspace iconForFile: x11Path];
  NSData* icon =[x11Icon TIFFRepresentation];
  return icon;
}



// ─────────────────────────────────────────────────────────────────────────────
// Get an icon from an X11 bitmap.
// Currently there is no real advantage in using CoreImage to do the scaling,
// but by applying more filters, it should be possible to get a higher quality
// scaling.
// ─────────────────────────────────────────────────────────────────────────────


NSString* fromStdString(const std::string& str) {
  return [NSString stringWithCString: str.c_str()encoding: NSISOLatin1StringEncoding];
}

// ─────────────────────────────────────────────────────────────────────────────
// Translate a bell event into a dictionary understood by Growl
// TODO: currently X11 icons are retreived and converted for each
// notification, they could probably be cached.
// ─────────────────────────────────────────────────────────────────────────────

NSDictionary* dictionaryForEvent(BellEvent* event, NSData* defaultIcon) {
  NSMutableDictionary* dictionary = [[NSMutableDictionary alloc] init];
  [dictionary setObject: @"Bell" forKey:  @"NotificationName"];
  [dictionary setObject: @"XKB" forKey: @"ApplicationName"];
  NSString* description = fromStdString(event->name());
  // If there is a window name, prepend it to the notification text
  if (!event->windowName().empty()) {
    NSString* windowName = fromStdString(event->windowName());
    description = [NSString stringWithFormat: @"%@: %@", windowName, description];
  }
  if (!event->hostName().empty()) {
    NSString* hostName = fromStdString(event->hostName());
    description = [NSString stringWithFormat: @"%@: %@", hostName, description];
  }
  [dictionary setObject: description forKey: @"NotificationDescription"];
  // Convert volume in the [0 … 100] range to a number between 0 and 4.
  NSNumber* priority = [NSNumber numberWithInt: (event->percent() - 50) / 25];
  [dictionary setObject: priority forKey: @"NotificationPriority"];
  // If there is a X11 icon, convert it to be used by growl.
  ImageProxy* image_proxy = event->imageProxy();
  if (image_proxy != nullptr) {
    IconProvider* provider = [[IconProvider alloc] initWithProxy: image_proxy];
    CIImage* image = [
                      CIImage imageWithImageProvider: provider
                      size: image_proxy->width() : image_proxy->height()
                      format: kCIFormatARGB8 colorSpace: CGColorSpaceCreateDeviceRGB() options: nil ];
    CGSize originalSize = [image extent].size;
    NSSize targetSize = NSMakeSize(256, 256);
    const double scale = targetSize.height / static_cast<double> (originalSize.height);
    const double ratio = originalSize.height / static_cast<double> (originalSize.height);
    CIFilter* scale_filter = [CIFilter filterWithName:@"CILanczosScaleTransform"];
    [scale_filter setValue: [NSNumber numberWithFloat: scale] forKey:@"inputScale"];
    [scale_filter setValue: [NSNumber numberWithFloat: ratio] forKey:@"inputAspectRatio"];
    [scale_filter setValue: image forKey:@"inputImage"];
    CIImage* outputImage = [scale_filter valueForKey:@"outputImage"];
    NSCIImageRep* repr = [NSCIImageRep imageRepWithCIImage: outputImage];
    NSImage* resultImage = [[NSImage alloc] initWithSize: targetSize];
    [resultImage addRepresentation: repr];
    NSData* icon_data = [resultImage TIFFRepresentation];
    [dictionary setObject: icon_data forKey: @"NotificationIcon"];
  } else {
  
    [dictionary setObject: defaultIcon forKey: @"NotificationIcon"];
  }
  return dictionary;
}

// ─────────────────────────────────────────────────────────────────────────────
// Argument parsing function and configuration globals.
// ─────────────────────────────────────────────────────────────────────────────


static struct option longopts[] = {
  { kDisplayArg, optional_argument, nullptr, 0},
};

char display[1024];

int parse_options(int argc, char* const * argv) {
  strncpy(display, getenv(kDisplayEnv), sizeof(display));
  while(true) {
    const int c = getopt_long_only(argc, argv, "d:", longopts, NULL);
    switch (c) {
      case -1:
        return 0;
      case 'd':
        strncpy(display, optarg, sizeof(display));
        break;
      case 0: // long parameter
        break;
      default:
        fprintf(stderr, kUsage, argv[0]);
        return EX_USAGE;
    } // switch
  } // while
}

// ─────────────────────────────────────────────────────────────────────────────
// Main function, parameter parsing and main loop.
// TODO: add some kind of clean-up exit mechanism
// ─────────────────────────────────────────────────────────────────────────────


int main (int argc, char* const * argv) {
  char* sandbox_error = nullptr;
  // Sandbox API is deprecated, but we want to be a unix process.
  if (sandbox_init(kSBXProfileNoWriteExceptTemporary, SANDBOX_NAMED, &sandbox_error) != 0) {
    fprintf(stderr, "could not initialise sandbox: %s", sandbox_error);
    sandbox_free_error(sandbox_error);
  }
  
  const int option_status = parse_options(argc, argv);
  if (option_status){
    return option_status;
  }

  // Set up objective-c stuff
  NSData* defaultIcon = getX11IconData();
  std::unique_ptr<X11DisplayData> x11Display(X11DisplayData::GetDisplayData(argv[0], display));
  id<GrowlNotificationProtocol> growlProxy = getGrowlProxy();
  
  while(true) @autoreleasepool {
    std::unique_ptr<BellEvent> event(x11Display->NextBellEvent());
    NSDictionary* eventDict = dictionaryForEvent(event.get(), defaultIcon);
    [growlProxy postNotificationWithDictionary: eventDict];
  }
  return EX_OK;
}
