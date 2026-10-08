# S7 Identity and Credential Provisioning

## Status

Design proposal for the S7 MVP. This document describes the intended provisioning workflow for X.509 node identity and the control Studio. Git hosting and Git credentials on runtime nodes are outside the MVP scope. It is not a production security specification; cryptographic profiles, credential storage details, and enrollment protocol remain to be finalized.

Related documents:

- `REMOTE_NODE_INFO_AND_DEPLOYMENT.md` — node identity and control-plane behavior
- `S7_Control_Protocol_MVP.md` — control messages and node identity binding
- `s7l2/docs/S7_Transport_Architecture.md` — TLS identity and network boundaries

## Goals

- Give each logical runtime node a persistent, verifiable identity.
- Provision node identity and repository access through one operator workflow.
- Let the single control Studio keep local Git history for diagrams and support review and rollback.
- Keep node private keys local and grant each credential only the access its role requires.
- Keep the credential format independent of the runtime implementation language.

## Identity Model

Each logical S7 node has a stable `node_uuid`. A process restart creates a new process `instance_id`, but does not change the logical node identity.

The node has separate credentials for separate purposes:

| Credential | Purpose | Secret stays on |
|---|---|---|
| X.509 private key and certificate | mTLS identity for S7 control/data connections | The node that owns the identity |
| SSH private key | Optional future authentication to a remote Git host | The Studio host that publishes repository content |

If remote Git hosting is used later, the SSH and X.509 private keys are distinct key pairs. Provisioning records their association with the same Studio identity, but one key is never derived from or substituted for the other. A local Git repository does not require an SSH key.

The runtime certificate identity is bound to the logical node using the SAN URI:

```text
spiffe://s7/node/<node_uuid>
```

The certificate identity is authoritative. Any `node_uuid` reported in a control-plane message must agree with the authenticated certificate identity. This follows the binding described in `REMOTE_NODE_INFO_AND_DEPLOYMENT.md`.

The MVP has one configured control Studio. Its certificate carries the S7 control-node key purpose in its X.509 v3 Extended Key Usage extension (next to `clientAuth`). Each runtime trust configuration pins the accepted key-purpose OID and the authorized Studio identity; adding or rotating the authorized Studio requires an explicit trust-configuration update. Multiple Studio nodes, master handover, and coordination between Studio operators are future work.

## Provisioning Workflow

The provisioning utility should support these high-level operations for runtime nodes and the control Studio:

1. **Create or select a logical node identity.** Create a persistent `node_uuid` for a new node, or reuse the existing identity when restoring that same logical node.
2. **Generate the X.509 key on the node.** Generate the private key locally. Private keys should not be exported as part of the normal enrollment flow.
3. **Enroll the X.509 identity.** Submit a certificate request to the configured S7 trust authority. The issued certificate binds the node UUID and includes the authorized node role and appropriate TLS key usages.
4. **Install trust roots and runtime configuration.** Install the S7 trust roots and node-local runtime configuration.
5. **Record the result.** Store non-secret identity metadata, certificate fingerprints, and validity information in a local provisioning record.
6. **Verify identity.** Confirm certificate-chain validation and the certificate-to-`node_uuid` binding.

Provisioning should be repeatable and should report whether each operation created, reused, rotated, or failed to install a credential. It must not silently create a new `node_uuid` when an existing node identity cannot be read.

## Git Repository Access

The MVP stores diagram projects in a local Git repository on the control Studio host. Git provides local commit history, branches, diffs, and rollback. Runtime nodes do not need Git credentials, Git client libraries, or Git network access in this MVP.

Studio selects a committed project revision and sends the required schema/configuration to runtime nodes through the S7 control/deployment path. A deployment should record an immutable project revision identifier and schema/artifact digest. Runtime nodes continue running their last committed configuration while disconnected from Studio.

Future Git hosting may use a configurable remote such as GitHub, GitLab, another Git host, or a repository served by a Studio node. When that is introduced, the host-side association of each SSH public key will determine repository and operation permissions. Public keys can be distributed; private keys must remain local.

Local Git history is a Studio storage feature; it does not define the schema format, schema validation, or deployment protocol.

## Key Handling and Lifecycle

- Private keys are never committed to Git or included in schema/diagram deployment artifacts.
- Do not copy one logical node's private keys to another node. A second logical node receives a separate `node_uuid` and separate keys.
- Protect private keys using the platform's available secure storage and access controls. The exact Linux, Windows, macOS, and service-account mechanisms are implementation-specific.
- Keep public-key fingerprints and certificate fingerprints in provisioning metadata; public keys themselves may be shared with Git access providers.
- Support certificate rotation without changing the logical `node_uuid`.
- On node retirement or compromise, revoke/expire the X.509 certificate.
- A restored node may reuse the same identity only when it is the same logical node and its private credential material remains under authorized control. Cloning a node image must not accidentally clone usable private credentials.

## Trust and Authorization Boundaries

The following checks are separate and all are required for their respective operations:

1. **S7 peer trust:** validate the X.509 chain, certificate validity, SAN node UUID, and authorized certificate role before accepting trusted S7 control/data operations.
2. **Configuration authorization:** validate that the single control Studio identity is authorized to request a configuration change, and apply the configured version/conflict rules.
3. **Artifact integrity:** validate the schema and record the immutable project revision/digest before activation.

Local Git access does not itself authorize a Studio to change the running network. The Studio's S7 identity and the control protocol govern that authorization.

## Cross-Language Implementation

Provisioning records and credential bindings must have a language-neutral representation. Each runtime implementation may use its platform's X.509 libraries, but must follow the same identity binding and artifact validation rules.

The Studio may use its local Git installation or an embedded Git library for project history. This is a Studio packaging choice and is not part of the runtime protocol. If remote Git access is added later, the implementation can choose an embedded Git client and SSH library without changing S7 deployment semantics.

## Example Provisioning Record

This is illustrative metadata only. It contains fingerprints and public identity information, never private key material.

```json
{
  "record_version": 1,
  "node_uuid": "3e89b2be-1a23-4bc8-a73d-7a99ae9c0df2",
  "instance_id": "6de8ef16-f924-4502-a486-8bcb4ce35e9e",
  "identity": {
    "certificate_san_uri": "spiffe://s7/node/3e89b2be-1a23-4bc8-a73d-7a99ae9c0df2",
    "certificate_fingerprint_sha256": "<certificate-fingerprint>",
    "role": "runtime-node"
  },
  "project": {
    "repository": "local",
    "revision": "<git-commit-id>",
    "schema_digest": "<schema-artifact-digest>"
  }
}
```

The format and storage location of this record are not yet standardized. It should be possible to regenerate it from the installed credentials and policy without exposing private key contents.

## Open Decisions

- Which service issues S7 certificates, and how initial trust roots are delivered and rotated.
- Numeric assignment of the S7 key-purpose OIDs under the organization's private enterprise arc; certificate key algorithms, profiles, and validity periods. Until then, development and tests use the UUID-derived placeholder `2.25.4266950079767543379770189370854073200` for the control-node role (`s7l2/tools/generate_test_runtime_certs.sh`).
- Whether project diagrams and executable schemas are stored as separate files or in one project document.
- How the Studio maps a Git commit to the exact schema/configuration sent to runtime nodes.
- Whether and how remote Git hosting is added after the MVP.
- Whether private-key protection uses OS keystores, encrypted files, hardware-backed keys, or configurable alternatives.
- Whether deployment artifacts require a signature in addition to the canonical JSON SHA-256 configuration digest and project revision identifier.

## Current Repository State

The repository contains test certificates under `s7l2/s7runtime/examples/certs/` and a script for generating them. These are development fixtures only. The runtime currently extracts certificate common name and SHA-256 fingerprint for control-plane metadata; this must not be confused with a complete provisioning, authorization, or certificate-bound role enforcement implementation.
