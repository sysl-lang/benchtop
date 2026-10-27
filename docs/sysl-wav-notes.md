# Sysl friction encountered while adding WAV recording

Using installed Sysl 0.7.0. No compiler change or language workaround blocked
this implementation. The allocation-free WAV library compiled for M0+, M4,
M33, RV32 and AArch64.

* Standard I/O has a Reader trait, but no corresponding common positioned
  read/write interface. The library defines small Source and Sink traits. A
  reusable, allocation-free standard interface for positioned I/O would reduce
  adapters across binary-format libraries and firmware/filesystem projects.
* Mutable Option payload state needs explicit extraction to a var followed by
  assignment back into Some. Recording keeps its FAT volume/file and WAV writer
  this way; a mutable payload access helper or borrowing pattern would make
  those updates less repetitive. Current Option helpers return values.
* The compiler accepted a FAT volume's cache view being passed back as its read
  output. At a cluster boundary this overwrote the cache and produced BadCluster
  in the regression's readback. Separate cache/output arrays fix it. This is a
  useful case for stronger overlapping-view diagnostics, or at least a clearer
  no-alias contract in buffer-based library APIs. It was a test buffer ownership
  mistake, not a WAV or firmware recording failure.

These are API/ergonomics observations, not claims of a confirmed compiler defect.


While implementing SDMMC under installed Sysl 0.7.2, mutable scalar captures in
callbacks were another small friction point: assigning to the captured copy did
not update the enclosing test's flag. Explicit reference-backed observation
state made ownership clear. Native SD and both firmware variants otherwise
compiled without language/compiler changes.
