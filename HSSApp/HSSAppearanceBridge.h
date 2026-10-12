#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN

@interface HSSAppearanceArtifacts : NSObject
@property(nonatomic,readonly) NSData *faceJSON;
@property(nonatomic,readonly) NSData *objData;
@property(nonatomic,readonly) NSData *mtlData;
@property(nonatomic,readonly) NSData *manifestData;
@property(nonatomic,readonly) NSArray<NSString*> *usedAssetIDs;
@end

/// App-internal copied-value adapter to the portable calibrated projector.
/// Never receives a mutable engine or emits replacement metric geometry.
@interface HSSAppearanceBridge : NSObject
+ (nullable HSSAppearanceArtifacts*)buildForProjectID:(NSString*)projectID
                                      sceneRevision:(uint64_t)revision
                                  sourceIsSynthetic:(BOOL)synthetic
                                  transformRevision:(uint64_t)transformRevision
                                        verticesXYZ:(NSData*)vertices
                                    triangleIndices:(NSData*)triangles
                                              views:(NSArray<NSDictionary<NSString*,id>*>*)views
                                              error:(NSError**)error
 NS_SWIFT_NAME(build(projectID:sceneRevision:sourceIsSynthetic:transformRevision:verticesXYZ:triangleIndices:views:));
@end
NS_ASSUME_NONNULL_END
