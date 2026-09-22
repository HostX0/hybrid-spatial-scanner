#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Immutable bytes produced by the three verified native exporters for one
/// accepted scene revision. This bridge is app-internal and does not extend
/// the public HSS C ABI.
@interface HSSExportArtifacts : NSObject

@property(nonatomic, readonly) NSData *pdfData;
@property(nonatomic, readonly) NSData *svgData;
@property(nonatomic, readonly) NSData *pdfSVGManifestData;
@property(nonatomic, readonly) NSData *dxfData;
@property(nonatomic, readonly) NSData *dxfManifestData;
@property(nonatomic, readonly) NSData *objData;
@property(nonatomic, readonly) NSData *mtlData;
@property(nonatomic, readonly) NSData *objManifestData;

@end

@interface HSSExportBridge : NSObject

+ (nullable HSSExportArtifacts *)buildArtifactsForProjectID:(NSString *)projectID
                                              sceneRevision:(uint64_t)sceneRevision
                                                   sceneJSON:(NSData *)sceneJSON
                                                  verticesXYZ:(NSData *)verticesXYZ
                                               triangleIndices:(NSData *)triangleIndices
                                                       error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
