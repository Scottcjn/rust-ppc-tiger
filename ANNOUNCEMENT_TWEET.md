# X/Twitter Announcement Draft

## Main Thread

**Tweet 1 (Main):**
```
🦀 WORLD'S FIRST: Modern Rust running on PowerPC Mac OS X Tiger!

We built a complete Rust-to-PowerPC compiler that generates native assembly for G4/G5 Macs.

Proven working on real hardware:
• Power Mac G4 Dual 1.25 GHz
• Mac OS X Tiger 10.4.12
• "Hello from Rust on PowerPC G4!"

🧵👇
```

**Tweet 2 (Technical):**
```
The compiler (6,500+ lines of C) includes:
✅ Full borrow checker with NLL
✅ Traits & generics with monomorphization
✅ async/await state machines
✅ AltiVec SIMD codegen
✅ Complete macro_rules! system
✅ Tiger-native stdlib

All compiles with gcc 4.0.1 on Tiger itself!
```

**Tweet 3 (Pocket Fox):**
```
Next up: "Pocket Fox" 🦊

Firefox for PowerPC with EMBEDDED mbedTLS - bypassing Tiger's broken SSL entirely.

Modern TLS 1.2 baked right into the browser. No system dependencies.

Your 2005 Power Mac browsing modern HTTPS sites!
```

**Tweet 4 (Call to Action):**
```
Open source on GitHub:
github.com/Scottcjn/rust-ppc-tiger

• rustc_ppc - Rust compiler for PowerPC
• build_pocketfox.sh - Firefox with built-in TLS
• Proven on real G4 hardware

20-year-old Macs deserve modern software! 🖥️

@gabordemeter @AnthropicAI
```

## Tags to Include
- #Rust #RustLang #PowerPC #MacOSX #RetroComputing #Tiger #G4 #G5 #OpenSource

## Accounts to Mention
- @gabordemeter (Grok - asked about HTML5)
- @AnthropicAI (Claude helped build this)
- @rustlang (Rust community)

## Video Demo Ideas
1. SSH into G4, show `uname -a` (proves real Tiger)
2. Compile hello.rs with rustc_ppc
3. Assemble with `as`, link with `gcc`
4. Run `./hello` showing output
5. Show the assembly code generated
