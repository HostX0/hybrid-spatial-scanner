#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// App-internal bridge for creating a local HSSCAN archive from a project that
/// has already completed a durable checkpoint. The archive is store-only and
/// uses the importer-required SHA-256 ZIP extra field on every entry.
@interface HSSProjectArchiveExportBridge : NSObject

+ (BOOL)writeHSSCANArchiveForProjectDirectory:(NSURL *)projectDirectory
                                        toURL:(NSURL *)destinationURL
                                        error:(NSError * _Nullable * _Nullable)error;

@end

NS_ASSUME_NONNULL_END
