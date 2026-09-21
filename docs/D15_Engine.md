# D15 — Reconstruction, Topology and Numerical Engine Specification

Engineering baseline v1.1. Normative implementation design for the offline hybrid engine. The reference pack implements selected numerical oracles and contracts, not the full scanning engine. Initial numeric thresholds are tuning hypotheses unless explicitly described as a file-format invariant or host numerical tolerance.

## 1. Responsibilities and trusted state

SpatialCore accepts calibrated observations, session-coordinate relationships, optional platform proposals and journaled human commands. It publishes immutable scene snapshots and localized quality issues. It must not own the camera, make HTTP requests, display UI, request permissions or write directly to user-selected cloud locations. Those responsibilities stay in the Swift application and adapters.

There are three linked representations. Observation storage preserves source depth, poses, mesh anchor revisions and selected images. The metric scene contains accepted surfaces, boundary graphs, regions, constraints and measurement provenance. Appearance and semantics contain textures, labels and optional schematic proxies. A label cannot manufacture a measured plane; a photorealistic texture cannot prove a clearance; a fitted plane cannot conceal the fact that its lower edge was occluded.

SceneSnapshot is the sole accepted geometry authority. Plan dimensions and captured/structured review identify its revision. A live candidate may be rendered with a provisional style, but exporters consume only an explicitly selected snapshot. A draft snapshot is allowed when its internal geometry is consistent and its incompleteness is preserved; invalid coordinate relationships are not clean exportable geometry.

## 2. Native execution graph

The application owns one ARSession and a CaptureCoordinator. The normalizer validates current frame dimensions, intrinsics, pose, tracking status and buffer availability. An ingest mailbox replaces expendable pending frame observations under load while keeping command and structural-control events ordered. A serial engine executor mutates the feature graph and schedules bounded dense work. The storage worker persists source chunks and accepted journal transactions. The renderer consumes immutable buffers; the exporter works against a frozen revision.

Only one component owns ARSession configuration changes. Record the effective configuration before and after enabling a RoomPlan adapter. Do not install competing delegates or reset tracking invisibly to start an additional subsystem. The documented custom-session initializer establishes a supported integration route, not evidence that an untested combination runs with the requested fidelity. [R01]

The first shipped capture path may use ARKit mesh for captured geometry while the custom fusion backend remains gated. Both paths must expose their provenance. Do not advertise high-detail custom reconstruction until its actual output, memory and device tests pass. The architecture permits incremental delivery without pretending a placeholder GPU kernel is a working scanner.

## 3. Units, coordinates and identity

All canonical physical coordinates are metres in a right-handed project frame with +Y up. Matrices are column-major 4 by 4 rigid transforms. Canonical geometry uses Float64. GPU positions use Float32 in bounded local submap coordinates, never an unnecessarily large global coordinate origin. Unit conversion happens at validated import and display/export boundaries only.

FloorFrame contains origin O and orthonormal U, V and N. The default is U=(1,0,0), V=(0,0,-1), N=(0,1,0), satisfying U cross V=N. Plan coordinates are u=dot(P-O,U), v=dot(P-O,V). A renderer may invert v for its screen convention exactly once. The captured project's orientation is not geographic north unless a separate supported reference supplies that relationship.

Every event has capture identity, session epoch, sequence and monotonic time. An AR tracking reset creates a new epoch. T_project_world is accepted per epoch and versioned. No geometry crosses epochs without an accepted relationship. A new epoch may remain a separate unresolved region; this is preferable to distorting an already-valid room.

Production entity IDs are UUIDs. In-memory sequences use uint64; JSON uses canonical decimal strings so JavaScript/agent tooling cannot silently round values above 2^53. Entity lineage records parent and replacement IDs through splits and merges. A nearby new surface is not automatically the same wall merely because its centre is close.

## 4. Calibrated depth ingestion

Use sceneDepth as the selected measurement input. It is public estimated depth, not a raw LiDAR return stream. smoothedSceneDepth averages over time and is reserved for preview or an explicitly separate experimental pipeline. Do not count both inputs as independent measurements of one scene. [R02]

For an axial positive-depth optical convention, calculate x=(u-cx)z/fx and y=(v-cy)z/fy. The AR camera convention maps that point to (x,-y,-z), followed by T_world_camera and the accepted epoch transform. The adaptor must prove the convention against a physical plane and the actual platform buffers, including off-axis pixels. Do not substitute the UI viewport transform for camera calibration. [R22]

Scale intrinsics to the actual depth-buffer resolution and crop relationship, not a remembered phone-specific resolution. Record RGB and depth dimensions, pixel formats and strides. Respect CVPixelBuffer plane layout and lock/unlock lifetimes. Copy or retain only under an explicit bounded ownership contract. A UI rotation affects presentation; it must not rotate recorded sensor coordinates twice.

Reject nonfinite or nonpositive depths, invalid calibration, malformed strides and invalid tracking. Retain a reason/counter for rejection. Optional confidence is ordinal and may be unavailable. Unknown confidence is not the same as zero depth; the normalized value 255 means unavailable. Initial low-confidence policy is exclusion from metric fusion and provisional use for visual preview, with controlled experiments permitted through a named tuning profile.

The camera pose and depth provided together can share correlated errors. Do not assign an invented covariance or repeatedly fuse near-identical stationary samples as independent evidence. Keyframes are selected for useful translation, rotation, quality and newly visible surface support. Initial 0.15 m/10-degree triggers are hypotheses, not requirements that a user must move that far for every face.

## 5. Surface-preserving preprocessing

Create a valid-sample mask, a discontinuity mask and, where justified, local normals. A normal requires an adequately conditioned neighbourhood and enough valid neighbouring samples. Never compute a stable-looking normal by averaging across a foreground shelf edge and the wall behind it. At strong depth discontinuities, keep separate support or mark a local fit uncertain.

Use filtering for the purpose it serves. A mild edge-aware depth representation can support normals or presentation, while retained measurement samples remain available for reconstruction and metrology. The original KinectFusion treatment distinguishes surface filtering and integration; its lesson is not to smooth away the details the product is meant to preserve. [R13]

Depth/RGB alignment may support edge cues or conservative dynamic masks, but an RGB edge alone does not supply depth or structural permanence. A moving-person mask is a rejection proposal with provenance, not licence to erase a fixed column. Absence of a semantic label must never delete valid observed geometry.

## 6. Sparse-block fusion model

Select a hashed block grid per local submap. A block contains 8 by 8 by 8 voxel cells. Structural spacing begins at 0.02 m; protected detail regions may use a separate 0.01 m layer. These are computation scales. They do not imply that every one-centimetre feature can be physically resolved.

Each voxel stores normalized TSDF, integration weight, support/visibility metadata and optional compact color. Unknown means integration weight zero. Its numeric TSDF default value must not be interpreted as a measured surface. Sign convention is positive in observed free space in front of the first measured surface and negative immediately behind it inside the truncation band.

Use a declared memory budget and measured allocation accounting. An illustrative 16-byte voxel occupies 8 KiB per 512-voxel block; 4,096 blocks consume 32 MiB before hash tables, staging, textures or double buffers. This is arithmetic, not a phone memory allowance. Configure resident blocks from the actual device budget and evict finalized inactive blocks to local storage. Do not advertise unlimited halls based on sparse allocation alone.

Separate block activation from voxel integration. The CPU constructs a unique, sorted active work list initially. GPU work items own distinct voxel indices, avoiding competing floating-point writes. Dispatch only validated local regions within the current frame's field of view. Guard every grid, index and projection bound. Integer hashing and block residency are deterministic in the CPU reference path. [R09]

## 7. TSDF update and zero crossings

For voxel centre P transformed into the current camera frame, project to a valid depth pixel and let z_v=-P_camera.z. Let d be the selected axial depth and eta=d-z_v. If depth/tracking/eligibility is invalid, skip. If eta is below -mu, skip: the voxel is behind the first visible surface beyond the uncertainty band. Otherwise f=clamp(eta/mu,-1,1).

For current value F and weight W, choose eligible weight w from declared quality factors. The baseline freezes saturation: w_eff=min(w,max(0,W_cap-W)). If w_eff is zero, leave the voxel unchanged. Otherwise F_new=(W*F+w_eff*f)/(W+w_eff) and W_new=W+w_eff. A later moving-average or decay policy is a different versioned algorithm; never implement it accidentally by capping W after using the wrong denominator.

The initial mu is 0.04 m and W_cap is 64 nominal weight units. Both require experiments. Sampling near small parallel faces is particularly sensitive to excessive truncation, edge bleed and support from correlated viewpoints. A smaller detail band may be used only under a named profile with a tested zero-crossing policy. A protected feature also retains source observations in the structural graph, so mesh smoothing cannot simply erase its existence.

Use nearest valid depth samples or an edge-aware interpolation policy that explicitly rejects crossing a depth discontinuity. Bilinear mixing of foreground and background can invent an intermediate surface. Color interpolation does not justify depth interpolation. Eligibility weights must exclude dynamic masks and invalid tracking before integration.

Extract a surface only across adequately supported neighbouring voxels with an accepted sign change. Both unknown-to-negative and unknown-to-zero transitions are not observed surfaces. Validate the mesher's topology and normals; filter display triangles independently of the metric graph. Marching-cubes case-table provenance/licence and boundary stitching require a tested implementation, not a hand-copied internet lookup table.

The included CPU scalar oracle and Metal reference kernel implement the same sign and saturation convention. Only the CPU scalar behavior was executed in the host reference tests. The Metal kernel has not been compiled or timed on a device and is not the full dense engine.

## 8. Submaps, pose correction and reintegration

Start a new local submap when the active region exceeds a bounded local extent, tracking changes materially, or a zone is explicitly finalized. Record each keyframe's submap membership and local pose. Do not let a submap boundary silently discard overlap or protected-feature evidence. Shared boundaries use explicit correspondence and uncertainty states.

A rigid change to T_project_submap can move an internally consistent submap without modifying its local voxel values. A correction to individual frame poses inside a fused submap changes its internal reconstruction. In that case rebuild the affected product from retained observations. Keep the old accepted version available until the rebuilt version passes validation and can be committed atomically. [R12] [R14]

Clamped TSDF values and saturated weights generally lose information required for exact subtraction. Do not claim reversible de-integration unless actual per-observation contributions and the exact arithmetic are retained. The selected baseline instead rebuilds dirty submaps from keyframes/depth records. A correction pipeline that cannot reproduce its sources must not make a metric improvement claim.

Global loop closure is not assumed to be solved by reading the current AR camera pose. Apple's internal historical pose graph is not an editable app API. Optional own pose-graph alignment must verify correspondence, geometric consistency, degeneracy and acceptance against independent checks. Moving a current-camera overlay is not equivalent to correcting the historical room model.

## 9. Structural surface proposals

Keep structural estimation independent of mesh simplification. Use spatially bounded point sets with source/frame IDs and capped support per correlated frame group. Segment by normal and depth discontinuities without imposing right angles. Create plane candidates with robust random sampling, then refine supported inliers with weighted total least squares.

A plane is n dot p + d=0 with ||n||=1. Minimal three-point hypotheses reject collinear samples. Score a candidate by independent support, spatial extent, residual distribution and viewing diversity. The reference oracle uses deterministic RANSAC and SVD; the production implementation additionally needs calibrated weighting and bounded candidate management.

Given inlier points p_i and weights w_i, compute centroid c=sum(w_i*p_i)/sum(w_i), covariance C=sum(w_i*(p_i-c)(p_i-c)^T). The smallest eigenvector is n; set d=-n dot c. Reject a neighbourhood with insufficient second principal variance, since a line cannot determine a stable plane. Fit residuals and support extent are stored, not just a success Boolean.

The initial distance inlier threshold is 0.02 m. It must be treated as a hypothesis and varied using declared evidence quality. A very low residual on a tiny patch is not sufficient wall support. A broad plane with many samples may be a tabletop; gravity and height are cues, not proof of category. Do not automatically classify every vertical surface as an enclosing wall.

Two candidate faces can merge only when normal/offset consistency, support overlap, adjacency, history and protected topology permit it. Similar normals are not enough: a wall and a cabinet front are often parallel. A front face 15 cm wide must not be removed by a global minimum-area rule designed for a large wall.

## 10. Boundaries and narrow features

Construct a persistent boundary graph with nodes, edges, surface support and feature lineage. Plane intersections propose edges only when well conditioned; nearly parallel planes require a bounded endpoint/contour fit or more evidence. An intersection far outside observed support is an extrapolation, not an observed corner.

At each relevant height policy, use support masks and intersections to propose contour fragments. Assemble fragments into candidate loops using adjacency and endpoint evidence. The outer loop is counterclockwise in the defined plan basis; holes are clockwise. Validate closure, self-intersections, duplicate vertices, near-zero edges and hole containment. Forward collinear edges may remain when they carry protected feature or semantic boundaries.

A canonical protected attached protrusion is an ordered chain: approach, first return, front, second return, continuation. Its dimensions, angles and symmetry are not hardcoded. The user's 0.50 m by 0.15 m example is an adversarial fixture, not a global rectangular-column assumption. Protecting the feature prevents deletion of the chain; its coordinates may still improve with new evidence.

A detached fixed column belongs to an interior obstruction footprint. It must not pull the outside boundary inward through a false connection. A column attached to a wall changes the outside contour. If attachment is uncertain, preserve a candidate and ask for a useful view or user classification before subtracting it as a hole.

Floating shelves remain overhead geometry unless their measured support reaches the floor and the chosen area policy treats them as fixed obstructions. Furniture is a distinct planning layer. A wall behind a wardrobe may be fitted from surrounding evidence, but that hidden wall patch remains unobserved. Do not manufacture an observed rear surface from a catalog object.

## 11. Area and projection rules

The measured floor footprint and the architectural cut are separate products. The cut defaults to 1.20 m with a declared 0.10 m band. A sloped wall's cut position may differ from its floor junction. Reflected-ceiling projection is another layer with local heights; it cannot reuse a single room height for coffers or slopes.

Compute signed area from closed validated rings using a translated local origin and compensated summation. Subtract fixed holes according to the selected area policy. Do not compute room area as the convex hull of all points; it would erase recesses, concavities and obstruction holes. Open boundaries yield provisional/unavailable area rather than an invented closure across an unseen doorway.

The exact authored rib fixture encloses 29.925 m2. A detached 0.30 m by 0.50 m column reduces its net fixed-obstruction area to 29.775 m2. These are mathematical expected values, not results from an iPhone scan. The reference tests deliberately include invalid self-crossing, touching-hole and zero-edge polygons so rejection behavior is also exercised.

## 12. Local constraint system

Each constraint identifies targets, source, units, value, state and origin command. Topology protection is not a numerical equality. A position lock freezes selected adopted coordinates; a distance equality constrains a specified relationship. Semantic labels do not enter the metric solver as fabricated measured dimensions.

The selected small local problem minimizes evidence and shape residuals subject to explicit hard equalities. Use state x in a local normalized coordinate frame. Soft residual r_k(x) is weighted by declared support and optionally a robust loss. Hard equalities c_j(x)=0 describe the adopted model. Tolerances for numerical feasibility must not be presented as measurement uncertainty.

For distance between points a and b, r=||a-b||-L. Its Jacobians are (a-b)/||a-b|| and the negative of that vector. Reject coincident endpoints before differentiating. A fixed coordinate uses c=x_i-v with unit Jacobian. A normalized parallel/orthogonal relation requires its own analytic Jacobian and scale convention. Production derivatives must be checked against central finite differences on synthetic cases; finite differences are a test oracle, not the chosen hot path.

The UI distinguishes exact-model dimension from soft measurement. A laser reading can have nonzero declared uncertainty even when the user elects to force the model to that value. A typed guess remains inferred. For genuinely interval-bounded measurements, either represent a declared soft residual within its uncertainty policy or implement a separately tested active-set inequality solver; do not pretend a scalar weight enforces an interval.

Ceres is not selected for general hard equalities because its public modeling FAQ explicitly says they are not built in. Its use for separately supported soft fitting remains possible. [R15]

## 13. KKT/SQP step and acceptance

For a local Gauss-Newton/robust approximation H=J^T W J + lambda D and gradient g=J^T W r, solve the saddle system [H A^T; A 0] [dx; nu] = [-g; -c], where A is the equality Jacobian. Normalize residuals and variable scales so metres, angular terms and weights are not mixed arbitrarily. Eliminate truly fixed variables first when that reduces rank problems.

Use a pivoted rank-revealing factorization and check residuals. Do not use an unpivoted positive-definite-only factorization on the indefinite KKT system. Redundant consistent constraints can be reduced; inconsistent linearized equalities generate a conflict or rejected step. A returned set of implicated constraints need not be minimal; label it honestly.

Apply a bounded step and line search on a merit function combining the soft objective and equality violation. Increase damping or reject if the step is nonfinite, expands outside the local problem, worsens geometry validity, crosses a protected topology boundary or fails sufficient improvement. Never accept a lower residual if it introduces a self-intersecting room.

Proposed runtime ceilings are 256 local scalar unknowns, 40 nonlinear iterations and a cancellable scheduled job. The host reference implements distance/coordinate-lock equalities and failure restoration, not all production residuals, robust weights, inequalities or device scheduling. Its feasibility threshold 1e-7 is a numerical test threshold in a small synthetic coordinate domain, not submicrometre sensing.

On convergence, verify equality feasibility, gradient/step stationarity, all geometry invariants and active entity IDs. On conflict, timeout, degeneracy or insufficient evidence, keep the last accepted snapshot. A preview may show the proposed correction with a warning, but the persisted graph must not be replaced by the failed state.

## 14. Transactional commands and reversal

A command carries identity, project, expected scene revision, ordered event sequence, target IDs, payload and intent. Validate syntax, then semantic preconditions, then current target lineage. A duplicate command ID with the identical canonical payload returns its recorded result. The same ID with a different payload returns DUPLICATE_MISMATCH; it cannot apply a second action under an old identity.

A command pipeline is validate, capture inverse/previous state, generate candidate, solve/validate, append a durable intent/result transaction, publish accepted snapshot. A C API success is not automatically an fsync. The UI may acknowledge receipt immediately, show application after the engine result, and show Saved only after journal/manifest commit. Distinguish those states explicitly.

Undo is an append-only compensating operation. It may need the affected local before-state, target lineage and original transform context, not merely an inverse number. A later surface observation must not make Undo target an unrelated replacement wall. Redo with stale incompatible topology returns a useful explanation. Never erase the source observations to simulate an undo.

Protect Feature disallows removal or reordering of its protected topology. Lock Position is separately explicit. Refinement cannot silently remove either constraint. A user may intentionally unlock or remove a protection through a recorded action; dismissing a warning is not such an action.

## 15. Live quality and next useful view

Maintain a quality vector rather than a global accuracy percentage: tracking state, valid depth fraction, support extent, independent view diversity, fit residual, change stability, target coverage, occlusion suspicion and conflicts. Confidence in a semantic label and confidence in a metric fit remain separate. A coverage percentage requires a defined target domain and its revision.

Prioritize validity/resource issues before completeness. Show one dominant actionable issue, keeping others in a tray. Initial noncritical debounce is 0.8 s, minimum display 2 s and repeated haptic suppression 8 s. Critical tracking loss bypasses debounce. Store these values in one tuning profile so UI and engine do not drift.

For a missing return, identify the specific feature and the absent side. For poor viewpoint diversity, suggest lateral motion or another angle. For weak camera tracking, request stable visible edges or illumination. Waiting is appropriate only when a real processing task is pending; it cannot reveal an occluded underside. Do not fabricate an exact 60 cm movement instruction unless accepted geometry supports it, and never imply collision-free navigation while the operator is looking at a phone.

A precise reticle hit requires current calibration, current accepted epoch and supported depth/geometry. Otherwise retain a pending ray/region and ask for confirmation or another view. An intersection with a fitted plane is fitted provenance, not a new observation. During relocalization, hide stale precise overlays instead of freezing them deceptively over the wrong wall.

## 16. Ceiling and furniture reconstruction

Observe arbitrary surface orientation. A ceiling can contain sloped regions, soffit bottoms, vertical returns, curved details and open skylight regions. Fit separate patches where supported; otherwise retain captured mesh. Do not fill sky/no-depth with the maximum sensor distance. Store local heights relative to the accepted floor reference.

The captured model preserves only observed visible geometry. A shelf's back or underside is unknown until a usable view supports it. Unknown category does not erase the shelf. A semantic proxy remains a schematic overlay and cannot alter observed clearance. Generic watertight completion is an inferred visualization option, not part of trusted measured geometry.

Texture projection requires validated RGB/depth geometry and occlusion tests. Select keyframes by quality, angle and coverage. A wall behind an object must not receive that object's texture because it projects to the same pixel. Exposure/seam blending changes appearance only. Use separate mesh/display LOD so rendering optimization cannot silently change dimension endpoints.

## 17. Resource and scheduling policy

Limit pending expendable frames to two initially and record their dropped count. Commands, mesh removals, epoch transitions and durable commit events are not silently dropped. Under pressure, reduce preview texture resolution, optional appearance work and unprotected display LOD before disabling validity checks. If the measurement path cannot remain safe, checkpoint and pause.

The initial target is adaptive 30–60 fps presentation, up to 10 Hz structural updates and up to 5 Hz quality presentation. The 30 ms incremental geometry, 100 ms input acknowledgment and 500 ms correction-preview p95 targets must be measured on declared devices and workloads. A full nonlinear job may exceed one render interval and must be scheduled/cancellable rather than falsely advertised as synchronous.

Observe thermal state and free storage during capture. Baseline storage warning/stop reserves are provisional. Never allocate the entire expected room volume or retain every full-resolution RGB image in RAM. Source chunks and keyframes are streamed to local storage with checkpoint bookkeeping. Inactive submaps can be paged, but the app must report a real resource limit instead of losing geometry silently.

## 18. Verification and release requirements

Host reference tests exercise C/C++ header layouts, C import from Swift, camera projection, transforms, synthetic ring validity/area, arbitrary plane fitting with outliers, equality feasibility/conflict, TSDF observation/saturation rules, anchor replacement and POSIX manifest-last recovery. Their actual logs are included in verification. These checks do not implement all methods described in this document.

Required remaining hardware tests include session coexistence, axial-depth convention, buffer orientation, frame timing, Metal parity, real narrow-feature capture, trajectory drift, multiroom seams, thermal endurance, low storage, interruptions and actual export interoperability. Physical metrology must identify reference instrument uncertainty and exact endpoints. A missing narrow face is a feature failure even when total area happens to look correct.

Use ablations to isolate changes: auto-only versus hybrid; platform mesh versus custom fusion; filtering on/off near edges; different truncation/voxel settings; topology protection on/off; texture refinement with identical geometry. Keep test scenes held out before tuning. Report errors and operator effort, not only attractive screenshots.

A globally competitive result remains an engineering objective. This architecture makes that objective measurable by ensuring source evidence, geometry, human intent and export state remain coherent and testable.
