#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// App-internal bridge for staging an untrusted HSSCAN archive. It validates
/// every entry before extraction and never adopts the staged project itself.
@interface HSSProjectImportBridge : NSObject

+ (BOOL)extractHSSCANArchiveAtURL:(NSURL *)archiveURL
                toStagingDirectory:(NSURL *)stagingDirectory
                             error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
