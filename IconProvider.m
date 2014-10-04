//
//  IconProvider.m
//  xkbgrowl
//
//  Created by Matthias Wiesmann on 2014/10/04.
//
//

#import "IconProvider.h"

@implementation IconProvider

- (id)initWithProxy: (ImageProxy*) proxy {
  image_proxy_ = proxy;
  return self;
}

- (void)provideImageData:(void *)data
             bytesPerRow:(size_t)rowbytes
                  origin:(size_t) x :(size_t)y
                    size:(size_t) width :(size_t)height
                userInfo:(id)info {
  image_proxy_->provideARGB(x, y, width, height, static_cast<char *>(data));
}
@end
