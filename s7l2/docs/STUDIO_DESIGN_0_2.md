# Studio design boundaries for 0.2.0

Studio is a client of the causal graph runtime. The 0.2.0 networking work adds
discovery, remote administration, deployment, and credential workflows, while
diagram resizing and printing add new editing and presentation paths. These
features need explicit owners so that Qt widgets do not become protocol or
runtime authorities. See [the release scope](RELEASE_0.2.0.md) and
[Phase 3](DEVELOPMENT_PLAN.md#phase-3-reduce-change-risk-in-high-churn-code).

## Ownership map

| State or behavior | Owner | Boundary for 0.2.0 work |
| --- | --- | --- |
| Diagram topology and undo history | `diagram::DiagramScene` and its model helpers | One user operation produces one undo command. Serialized topology comes from the scene. |
| Open document metadata, run settings, deployed node and stream references, saved baseline | `DocumentSession` | A deployment result updates this state; opening a document prepares a candidate before retiring the current run. |
| Blocking Studio control requests | `ui::NetworkTaskRunner` and `main_window_network.cpp` | Workers receive value snapshots. UI completions run on the owner thread; the owner joins its worker on destruction. |
| Discovery parsing and trust decisions | `ui::NodeDiscovery` | A worker checks the certificate presented by each contacted node. Peer reports remain unverified claims; a requester can be injected for tests. |
| Discovery UI | `ui::HeadlessNodesPanel` | Collects settings, owns the worker lifecycle, and presents service results on the UI thread. |
| Observation data | `ObservationSource` and live acquisition | Viewers read data; they do not mutate the runtime graph. |
| Viewer-specific presentation | Optional `LiveObservationViewer` and `ComparisonObservationViewer` capabilities | `ViewerManager` calls supported capabilities without depending on concrete window classes. |
| Runtime node listener settings and control authorization | Headless runtime/control plane | Studio must use a defined administration protocol. Diagram JSON and UI trust labels cannot grant control authority. |

## SOLID assessment and next extraction points

- **Single responsibility:** `MainWindow` still owns many UI actions. Network
  actions now have their own translation unit; document state and network task
  lifecycle have dedicated owners. Extract future settings and printing
  controllers around their own state and tests, rather than moving methods
  solely to shorten the file.
- **Open/closed and interface segregation:** viewer types register themselves.
  Live acquisition and comparison overlays now have small optional interfaces;
  ordinary viewers no longer inherit no-op operations. Registration remains
  the extension point for new observation views.
- **Liskov substitution:** every registered viewer must support the base
  presentation operations. A caller must test an optional capability before
  invoking it; type names do not imply a C++ subclass.
- **Dependency inversion:** deployment compilation and discovery accept
  requester callbacks. The next network increment should put configuration
  read/validate/stage/commit and restart behind a Studio service interface
  with fake transports for tests. Keep protocol authorization in the headless
  transport/control layer.

## Required follow-up before 0.2.0 networking UI

1. Introduce one Studio network settings model for seeds, CA/client credential
   paths, and refresh preferences. Bind both Settings and the nodes panel to
   it. Keep this separate from diagram `runtime_nodes` and runtime node startup
   configuration as specified in the release plan.
2. Define typed administration requests and results, including effective
   configuration, revision conflicts, apply timing, and restart ownership.
   Test with fake requesters before wiring dialogs. Do not interpret
   `GET_NETWORK_CONFIGURATION` as startup/listener configuration.
3. Continue scene extraction around connection rules, routing, and gesture
   state before adding resize handles. Keep geometry undo and wire attachment
   in one model transaction. Current Scope/LiveOscilloscope port growth has a
   regression for one-command undo/redo.
4. Extend the bounded retained-run comparison projection to result export and
   graph printing where responsiveness requires it. Viewer binding now yields
   between 4096-sample pages for large comparisons. Define a coherent viewer
   snapshot for printing without pausing acquisition.

Release evidence still needs real multi-node/restart/configuration cycles,
authorization failures, and UI responsiveness under slow network operations.
The focused tests on this branch cover the refactored ownership and existing
trust behavior; they do not qualify those 0.2.0 features yet.
