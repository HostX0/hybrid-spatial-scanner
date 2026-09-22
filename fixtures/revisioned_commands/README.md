# Revisioned command fixtures

These synthetic JSON files exercise byte identity only. The pretty-printed and
compact command files describe the same command, but their UTF-8 bytes differ;
reusing the command ID with the compact form must therefore report duplicate
mismatch rather than canonicalizing either document.

The result fixture explicitly says `not_committed`. The standalone contract
module retains bytes in process memory and does not implement or claim journal,
manifest, filesystem, save/reopen, or crash-recovery durability.
