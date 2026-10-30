# Hazard3 additional instruction vectors

Independent Python mathematics and LLVM-assembled instructions; every vector checks the
whole register/RAM state across all five profiles. Standard instructions are shared with
existing profiles, not separate Hazard3 implementations. The stateful extension tests
(CSR/traps/PMP/IRQs/power/Zcmp restart) live in `hazard3_tests.sysl`.

| Instruction | Vectors |
|---|---:|
| SH1ADD | 1170 |
| SH2ADD | 1170 |
| SH3ADD | 1170 |
| ANDN | 1170 |
| ORN | 1170 |
| XNOR | 1170 |
| MIN | 1170 |
| MAX | 1170 |
| MINU | 1170 |
| MAXU | 1170 |
| ROL | 1170 |
| ROR | 1170 |
| BCLR | 1170 |
| BSET | 1170 |
| BINV | 1170 |
| BEXT | 1170 |
| PACK | 1170 |
| PACKH | 1170 |
| CLZ | 85 |
| CTZ | 85 |
| CPOP | 85 |
| SEXT.B | 85 |
| SEXT.H | 85 |
| ZEXT.H | 85 |
| ORC.B | 85 |
| REV8 | 85 |
| BREV8 | 85 |
| ZIP | 85 |
| UNZIP | 85 |
| RORI | 1600 |
| BCLRI | 1600 |
| BSETI | 1600 |
| BINVI | 1600 |
| BEXTI | 1600 |
| H3.BEXTM | 7280 |
| H3.BEXTMI | 6880 |
| AMOADD.W | 2000 |
| AMOSWAP.W | 2000 |
| AMOXOR.W | 2000 |
| AMOOR.W | 2000 |
| AMOAND.W | 2000 |
| AMOMIN.W | 2000 |
| AMOMAX.W | 2000 |
| AMOMINU.W | 2000 |
| AMOMAXU.W | 2000 |
| LR.W | 2000 |
| SC.W | 2000 |
| FENCE.I | 1 |
| C.ZEXT.B | 32 |
| C.SEXT.B | 32 |
| C.ZEXT.H | 32 |
| C.SEXT.H | 32 |
| C.NOT | 32 |
| C.MUL | 400 |
| C.LBU | 176 |
| C.LHU | 88 |
| C.LH | 88 |
| C.SB | 176 |
| C.SH | 88 |

Stateful instruction tests in `hazard3_tests.sysl`:

| Instruction | Targeted coverage |
|---|---|
| CSRRW | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |
| CSRRS | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |
| CSRRC | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |
| CSRRWI | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |
| CSRRSI | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |
| CSRRCI | All source register/immediate fields, x0 suppression, aliases, CSR state changes; privilege/read-only/unknown CSR rules |
| MRET | Machine/User return, MIE/MPIE/MPP/MPRV and IRQ context stack |
| WFI | Sleep retirement, enabled-pending wake with global MIE clear, TW |
| H3.BLOCK | Paired wake, latched events, consume once, TW, MSLEEP depth |
| H3.UNBLOCK | Peer/sender signalling, pre-arriving events and TW |
| CM.PUSH | All 12 register lists and 4 stack adjustments; exact saved layout, restartable faults |
| CM.POP | All lists/adjustments, register restore, SP commit and restartable faults |
| CM.POPRET | All lists/adjustments, restore and return |
| CM.POPRETZ | All lists/adjustments, restore/return/a0 commit; partial-transfer faults |
| CM.MVSA01 | All 64 register pairs; reject identical destinations |
| CM.MVA01S | All 64 register pairs; allow identical sources |

Additional state tests cover all 52 IRQ inputs, all priority windows and unimplemented
windows, priority ties/nesting, force clearing/read suppression, CLEARTS, PMP granularity/
locks/first-match/partial-fetch rules and read/execute permissions (E6), MPRV, M-mode region
enforcement, User-mode interrupt masking (E7), counters, LR/SC/AMO faults and paired-hart
reservation invalidation/unblock signals. These are targeted architectural boundaries, not
exhaustive state enumeration or physical-board differential validation.

59 additional vector-tested instructions plus 16 named stateful instructions, 67,332 vectors; five profiles.

Regenerate with `python3 examples/riscv/generate-hazard3-tests.py` or verify with `--check`.
