//
//  IconProvider.h
//  xkbgrowl
//
//  Created by Matthias Wiesmann on 2014/10/04.
//
//

#import <Foundation/Foundation.h>
#import <QuartzCore/CIImageProvider.h>
#include "X11Util.h"

@interface IconProvider : NSObject {
  ImageProxy* image_proxy_;
}

- (id)initWithProxy: (ImageProxy*) image_proxy;

- (void)provideImageData:(void *)data
          bytesPerRow:(size_t)rowbytes
          origin:(size_t) x :(size_t)y
          size:(size_t) width :(size_t)height
           userInfo:(id)info;
@end
