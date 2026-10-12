#import "HSSProjectArchiveExportBridge.h"

#include "HSSProjectArchiveExport.hpp"
#include <exception>

namespace {

NSString *const kHSSProjectArchiveExportErrorDomain = @"org.hybridspatialscanner.hsscan-export";

}  // namespace

@implementation HSSProjectArchiveExportBridge

+ (BOOL)writeHSSCANArchiveForProjectDirectory:(NSURL *)projectDirectory
                                        toURL:(NSURL *)destinationURL
                                        error:(NSError * _Nullable * _Nullable)error {
  if (!projectDirectory.isFileURL || !destinationURL.isFileURL) {
    if (error != nil) {
      *error = [NSError errorWithDomain:kHSSProjectArchiveExportErrorDomain
                                    code:1
                                userInfo:@{NSLocalizedDescriptionKey: @"HSSCAN export requires local file URLs."}];
    }
    return NO;
  }
  hss::native_export::Result result;
  try {
    result = hss::native_export::write_portable_hsscan_archive(
        projectDirectory.path.UTF8String, destinationURL.path.UTF8String);
  } catch (...) {
    result = {hss::native_export::Status::kIoError, "Portable export failed safely. The local source project was preserved."};
  }
  if (result.status == hss::native_export::Status::kOk) return YES;
  if (error != nil) {
    NSString *message = [NSString stringWithUTF8String:result.message.c_str()];
    *error = [NSError errorWithDomain:kHSSProjectArchiveExportErrorDomain
                                  code:static_cast<NSInteger>(result.status)
                              userInfo:@{NSLocalizedDescriptionKey: message ?: @"HSSCAN export failed."}];
  }
  return NO;
}

@end
