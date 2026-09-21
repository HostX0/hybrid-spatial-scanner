# D14 — Research-Backed Architecture and Dependency Decisions

Engineering baseline v1.1. This document records a selected architecture, the primary evidence behind it, and the experiments required before implementation claims. It does not claim that a new scanner has been built, that every source repository was audited line by line, or that the proposed product already outperforms competitors.

## 1. Decision and product focus

Build a native offline scanning application with a Swift application layer, a portable C++ geometry and topology core, and a replaceable Metal dense-fusion backend. The backend lives inside the app. It is not a web API and does not require a Python server. A deterministic CPU reference implementation supplies numerical oracles; it is not the production throughput target.

One capture session supplies camera poses, calibrated public depth estimates and mesh updates. An optional RoomPlan adapter supplies semantic/parametric proposals when the chosen configuration is proved compatible. A metric feature graph owns accepted walls, corners, openings, floor regions, fixed obstructions, reference dimensions and human constraints. Captured mesh and appearance are separate representations linked to that graph by provenance and coordinates.

The initial market problem is difficult indoor capture with immediate operator assistance. Competing through another attractive rectangular room rendering is insufficient. The development focus is preserving concavities and small returns, explaining missing evidence before the operator leaves, allowing corrections during capture, exporting consistent geometry, and surviving offline interruptions. These are product decisions derived from the user's stated requirements, not claims of patent novelty or market exclusivity.

## 2. What the research changes

The v1.0 baseline already selected an embedded Swift/C++ design, offline operation, arbitrary angles, explicit provenance and replay. This revision makes the dense reconstruction, constraints and interface decisions more specific. It adds a bounded sparse-block fusion design; submap and pose-correction ownership; a formal distinction between soft fit residuals and hard model constraints; complete contract declarations and schemas; executable reference tests; and interaction diagrams.

A public repository is evidence that a method or implementation exists, not proof that it meets this product's device, licence, latency, precision or maintenance requirements. The research register identifies actual inspected documents and selected source files. A Git blob SHA identifies file content, not the entire project commit. No unverified commit hash is supplied. Dependency lock fields must be populated from the exact artifact adopted during integration, not guessed now.

## 3. Platform capture: reuse the correct layer

Apple documents an initializer that accepts an existing ARSession for RoomCaptureView. That permits a shared-session integration strategy, but the exact requested combination of frame semantics, reconstruction and session ownership still needs a physical-device spike. Do not create two independently tracked camera sessions and assume their coordinates align. The application must observe and record the configuration that actually runs. [R01]

Apple describes smoothedSceneDepth as temporally smoothed compared with sceneDepth. Therefore the selected measurement stream is sceneDepth; smoothed depth is an optional presentation input. Never fuse both as independent sensor evidence. The application receives public estimated depth, not unrestricted access to every raw LiDAR sensor return. Unsupported or absent data must be recorded as absent rather than synthesized. [R02]

ARKit's estimated mesh is a useful initial capture and preview representation. Plane-related processing can change its appearance, so preserve independent calibrated depth observations where the product needs to revisit a small protrusion. A clean platform mesh is not proof that a 15 cm front face was observed correctly. [R03]

RoomPlan already contains guidance instructions. Our distinction is a persistent association between an issue and a particular feature, the underlying evidence, an actionable request, and an accepted reversible human correction. It would be inaccurate to advertise that existing tools offer no coaching. [R04]

## 4. Mobile implementation references

RTAB-Map is a meaningful mobile reference because its repository includes actual iOS integration. The inspected NativeWrapper.hpp exposes a C linkage boundary, opaque creation/destruction, pose and intrinsics input, RGB/depth/confidence inputs, cancellation, recovery and mesh-export operations. This supports studying the separation between the native interface and processing core. It does not justify copying the entire application or its long positional-parameter functions. Our ABI uses explicit versioned messages, capacities and ownership rules instead. [R06]

StandardCyborgCocoa separates a C++ processing core from platform wrappers and includes recorded-input tooling. Its principal use case is TrueDepth object scanning, not this application's rear-LiDAR whole-room reconstruction and editable metric plan. It is an architecture and testability reference, not the chosen engine. Root repository licensing and every reused source/component would require checking; no code is imported from it in this handoff. [R07]

Direct Swift/C++ interoperability is a legitimate alternative, but it is not a reason to expose arbitrary STL ownership and exceptions across the application boundary. The selected baseline remains a narrow C facade. The reference pack type-checks its C import with the host Swift compiler, while iOS target builds and runtime lifetimes remain separate tests. [R05]

## 5. Dense reconstruction references

KinectFusion explains the relationship among calibrated depth, pose, volumetric fusion and surface prediction. Two especially important distinctions are an observed surface zero crossing versus an unobserved voxel, and camera tracking versus integration of a frame into a model. Its hardware-specific performance cannot be transferred to an iPhone. The proposed implementation adapts the geometric concepts while using the device's public tracking stream initially. [R13]

Open3D's tensor reconstruction material separates sparse block activation from dense integration. We adopt that decomposition as a design pattern: CPU-side deterministic work-list construction initially, then bounded per-voxel work on Metal. Open3D itself is selected as a desktop comparison environment, not an automatic binary dependency inside the mobile app. Its APIs, build support, memory and transitive dependencies must be evaluated for any future mobile adoption. [R08] [R09]

Voxblox is useful for its layer/block decomposition, weighting options and CPU mapping structure. Its ROS integration and ESDF planning use cases are not requirements of this scanning product. Voxgraph illustrates alignment of overlapping signed-distance submaps. Those ideas inform the submap boundary, but neither source proves that this app has solved loop closure or is safe for robot navigation. [R10] [R12]

BundleFusion demonstrates why pose corrections and reconstruction corrections cannot be separated casually. If an observation was fused using an old pose, moving the displayed camera alone does not repair the already-fused geometry. Our rule is to retain observation membership per submap and mark affected fusion products dirty when their accepted transforms change. Rebuild rather than subtract from clipped, saturated accumulators unless exact reversible contributions were explicitly retained. [R14]

## 6. Constraints: why a generic solver is not the whole engine

The Ceres modeling FAQ states that general equality constraints are not built in. Its parameter bounds are not a general constrained-geometry solver. A very large least-squares weight does not make a wall length a mathematically enforced equality, and may worsen conditioning. We therefore do not choose Ceres as the authority for live hard constraints. It remains useful as a separately benchmarked soft-fitting or pose-optimization comparator. [R15]

The selected local constraint path is a small bounded equality-constrained solver with analytic Jacobians, rank checks, scaled variables, a KKT/SQP step, damping and a merit line search. Locked coordinates can be eliminated before solving. A constraint that is numerically feasible describes the adopted model; it does not mean a physical laser measurement has zero uncertainty. Exact-model constraints and soft measurements are distinct user actions. D15 defines the solver and failure contract.

Eigen is the selected candidate for small reviewed linear-algebra operations, not a blanket requirement to import its entire unsupported module collection. Its 3.4.0 licence readme describes MPL2, other components and EIGEN_MPL2_ONLY. That guard and an exact artifact review are part of the dependency policy. The reference pack does not vendor Eigen, so its host tests do not prove an Eigen iOS build. [R16]

## 7. Polygon processing and triangulation

Clipper2 is a candidate for Boolean operations and offsets on validated, scaled polygon inputs. The README reviewed during this research contains an explicit caution about its triangulation code. The baseline therefore excludes that triangulation component. A future pin may be reevaluated only after regression evidence; documentation must not silently assume the warning no longer applies. [R17]

Earcut.hpp is a candidate for rendering already-valid planar rings with holes. Its own README does not guarantee correctness for every degenerate or malformed polygon. The metric boundary and area calculation remain independent of triangulation. Validate orientation, containment, intersections, triangle coverage and signed-area consistency. If triangulation fails, export boundary geometry with a warning instead of changing a narrow return to make triangles look clean. [R18]

For the current reference pack, simple polygon validation and area calculations are independently authored, small test oracles. They are not represented as production-grade robust predicates for arbitrary coordinate magnitudes. Production uses a declared extent bound and reviewed robust predicates; tolerance values are expressed in the applicable unit rather than scattered anonymous epsilons.

## 8. Learned models and research data

RoomFormer is relevant research for proposing floor-plan polygons from scene-derived inputs. A learned polygon predictor can be a comparator or candidate generator, but must not replace evidence-preserving topology, explicit small-feature protections or user constraints. Deployment on iPhone requires an actual model conversion, memory/latency test and suitable licence for code and weights. None is claimed by this package. [R19]

ARKitScenes offers mobile RGB-D and scene-understanding assets. The inspected licence contains noncommercial language and additional conditional commercial terms; treating it as an unconditional permissive dataset would be incorrect. Its use requires checking the exact version, component notices and the intended organization/use. No dataset has been downloaded, redistributed or accepted on the owner's behalf. Independently consented scans collected using this app remain mandatory for its actual capture characteristics. [R20] [R21]

No cloud language model participates in the trusted metric path. An optional review export may ask for semantic hypotheses or useful recapture advice. An external suggestion has neither access to hidden surfaces nor authority to invent dimensions. Generated geometry, relighting, catalog replacement and texture improvement must remain removable appearance/inferred layers, never silently become measured evidence.

## 9. Selected dependency roles

Apple ARKit and RoomPlan: platform capture/tracking and optional proposals; imported through native APIs; hardware compatibility gate required. Swift application code: lifecycle, UI, calibrated input adaptation, durable storage coordination and local sharing. C++20 core: geometry, topology, revisioned commands, bounded solver and export-ready scene. Metal: selected dense-compute backend, initially a candidate until parity and device gates pass.

Eigen: candidate numeric subset after exact pin and licence review. Clipper2: candidate Boolean/offset subset, triangulation excluded. Earcut.hpp: candidate display triangulation after independent validity checks. Open3D: development workstation oracle and comparison environment. RTAB-Map: integration/reference benchmark, not a wholesale mobile dependency. Voxblox/Voxgraph: reconstruction design references, no ROS runtime requirement. Ceres: optional comparison for supported soft optimization problems. RoomFormer: optional research comparator, not shipping core. StandardCyborgCocoa: TrueDepth architecture reference, not room-scanning substitute.

All optional dependencies must have a named owner, supported target architectures, exact revision or release hash, reproducible build recipe, licence/notice inventory, memory/size measurement, security review and exit strategy. A dependency is not approved merely because this document mentions it. The shipping build has no latest-branch downloads or unpinned model weights.

## 10. Rejected shortcuts

Do not derive floor area from a convex hull of arbitrary scene points. Do not orthogonalize every wall. Do not infer wall thickness from decorative double lines. Do not call a furniture bounding box a faithful mesh. Do not allow texture enhancement to alter dimensions. Do not assume missing depth is an opening. Do not normalize away a session discontinuity. Do not claim per-frame depth confidence is a calibrated probability. Do not make a reference numerical tolerance a advertised sensor accuracy. Do not call schema validation a complete security audit.

Do not split a single project into unrelated local/server geometry authorities. A backend can later provide accounts, sharing and jobs, but baseline capture, correction and export must remain useful without it. Do not require the user to publish a private room scan merely to obtain a plan.

## 11. Experimental decisions that remain open for a reason

The exact supported room size, feature-width reliability, capture distance envelope, CPU/GPU budget and materials domain must come from device experiments. Device-specific thresholds should be selected from development captures and evaluated on held-out scenes. The initial 0.02 m structural grid and 0.01 m detail grid are processing scales, not measured resolution. A 0.04 m fusion truncation band is an initial hypothesis and may need a smaller edge-aware or feature-specific treatment.

The precise Apple SDK/compiler release used for production is pinned only after a successful native build. The recorded host Swift/Clang versions prove only the reference checks executed here. The first compatibility spike must capture actual session configuration, available frame semantics and buffers, callback ownership, frame timing, power/thermal behavior and a recovery attempt. A failed combination must lead to an explicit ARKit-led mode with reduced proposal capability, not fabricated fields.

## 12. Source-to-decision traceability

The accompanying research/sources.json and sources.csv provide R01–R22 titles, URLs, observations and product decisions. D15 cites method-related IDs; D16 cites platform and ABI-related IDs; D17 uses the source-independent user journeys and test policy. Source observations are distinguished from original implementation choices throughout. No source's reported centimetre accuracy, benchmark speed or largest-dataset claim is adopted as a result for this app.

The inspected source file identities currently recorded are the RTAB-Map native header, the Voxblox root licence and the ARKitScenes licence. They are file blob SHAs, not repository commit locks. The handoff contains independently authored reference code and links; it does not redistribute third-party projects, research papers, trained models or datasets.

## 13. Five-phase impact

Phase 1 retains the user's live correction workflow and adds concrete wireframes. Phase 2 now consumes complete interface/schema files and a host-tested C import rather than illustrative declarations. Phase 3 implements bounded reconstruction, topology and solver contracts against executable fixtures. Phase 4 validates exported units, visibility/provenance and crash-safe project handling. Phase 5 separates reference math, desktop comparison and iPhone metrology outcomes so that documentation progress cannot be confused with product readiness.

The reference architecture is selected to make failures observable and improvements testable. The route to a globally competitive product is repeated measured improvement on difficult scenes, not unconditional confidence in a single library or a larger language model.
