#import "HSSProjectArchiveExportBridge.h"

#include "HSSProjectArchiveExport.hpp"

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
  const hss::native_export::Result result = hss::native_export::write_hsscan_archive(
      projectDirectory.path.UTF8String, destinationURL.path.UTF8String);
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
