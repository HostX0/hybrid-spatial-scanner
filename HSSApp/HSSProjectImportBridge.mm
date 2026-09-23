#import "HSSProjectImportBridge.h"

#include "HSSProjectArchiveImport.hpp"

namespace {

NSString *const kProjectImportErrorDomain = @"org.hybridspatialscanner.project-import";

void set_error(NSError **error, NSInteger code, const std::string &message) {
  if (error == nullptr) return;
  NSString *description = [[NSString alloc] initWithBytes:message.data()
                                                   length:message.size()
                                                 encoding:NSUTF8StringEncoding];
  *error = [NSError errorWithDomain:kProjectImportErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : description ?: @"The project archive could not be imported."}];
}

}  // namespace

@implementation HSSProjectImportBridge

+ (BOOL)extractHSSCANArchiveAtURL:(NSURL *)archiveURL
                toStagingDirectory:(NSURL *)stagingDirectory
                             error:(NSError **)error {
  if (archiveURL == nil || stagingDirectory == nil || !archiveURL.isFileURL || !stagingDirectory.isFileURL) {
    set_error(error, 1, "A local archive and private staging directory are required.");
    return NO;
  }
  const auto result = hss::native_import::extract_hsscan_archive(
      archiveURL.path.UTF8String == nullptr ? "" : archiveURL.path.UTF8String,
      stagingDirectory.path.UTF8String == nullptr ? "" : stagingDirectory.path.UTF8String);
  if (result.status == hss::native_import::Status::kOk) return YES;
  set_error(error, static_cast<NSInteger>(result.status), result.message);
  return NO;
}

@end
