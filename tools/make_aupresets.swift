// Writes the factory presets of every installed OP-1 AU (manufacturer "Bomb") as .aupreset files, grouped into folders.
//   swiftc -O tools/make_aupresets.swift -o /tmp/make_aupresets && /tmp/make_aupresets <output dir>
// Logic lists AU factory presets as one flat submenu, but shows folders for .aupreset files under
// ~/Library/Audio/Presets/<manufacturer>/<plugin>/. Each file is the AU's own ClassInfo after selecting the factory
// preset, so it restores exactly what the factory preset does. Needs the AUs installed (tools/make_mac_au.sh's install.sh).
import AudioToolbox
import Foundation

let outRoot = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "aupresets")

// First match wins. "Init" stays at the top level of the plugin folder.
let groups: [(folder: String, words: [String])] = [
    ("01 Vocals", ["vocal", "lead", "ballad", "ad-lib", "adlib", "throw", "word"]),
    ("02 Drums", ["drum", "kick", "snare", "hat", "slam", "delay rtn"]),
    ("03 Bass and Keys", ["808", "keys", "bass", "pad", "chorus"]),
]

func folder(for name: String) -> String? {
    let n = name.lowercased()
    if n == "init" { return nil }
    for g in groups where g.words.contains(where: { n.contains($0) }) { return g.folder }
    return "04 FX"
}

func fourCC(_ s: String) -> UInt32 { s.utf8.reduce(0) { ($0 << 8) | UInt32($1) } }
func fileSafe(_ s: String) -> String { s.replacingOccurrences(of: "/", with: "-") }

var desc = AudioComponentDescription(componentType: 0, componentSubType: 0, componentManufacturer: fourCC("Bomb"),
                                     componentFlags: 0, componentFlagsMask: 0)
var comp: AudioComponent? = nil
var total = 0
var plugins = 0
while true {
    comp = AudioComponentFindNext(comp, &desc)
    guard let c = comp else { break }
    var cd = AudioComponentDescription()
    AudioComponentGetDescription(c, &cd)
    var cfName: Unmanaged<CFString>?
    AudioComponentCopyName(c, &cfName)
    let full = (cfName?.takeRetainedValue() as String?) ?? ""
    // "Manufacturer: Plugin" is how Logic splits the preset folder path.
    let parts = full.components(separatedBy: ": ")
    guard parts.count == 2 else { continue }
    var inst: AudioComponentInstance?
    guard AudioComponentInstanceNew(c, &inst) == noErr, let au = inst else { continue }
    defer { AudioComponentInstanceDispose(au) }
    AudioUnitInitialize(au)
    defer { AudioUnitUninitialize(au) }

    var size = UInt32(MemoryLayout<CFArray?>.size)
    var presets: Unmanaged<CFArray>?
    guard AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &presets, &size) == noErr,
          let list = presets?.takeRetainedValue(), CFArrayGetCount(list) > 1 else { continue }

    plugins += 1
    for i in 0..<CFArrayGetCount(list) {
        let p = CFArrayGetValueAtIndex(list, i).assumingMemoryBound(to: AUPreset.self).pointee
        let name = (p.presetName?.takeUnretainedValue() as String?) ?? ""
        var preset = p
        guard AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &preset,
                                   UInt32(MemoryLayout<AUPreset>.size)) == noErr else {
            FileHandle.standardError.write("cannot select \(full) / \(name)\n".data(using: .utf8)!)
            continue
        }
        var info: Unmanaged<CFPropertyList>?
        var infoSize = UInt32(MemoryLayout<CFPropertyList?>.size)
        guard AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &info, &infoSize) == noErr,
              var dict = info?.takeRetainedValue() as? [String: Any] else { continue }
        dict["name"] = name
        var dir = outRoot.appendingPathComponent(fileSafe(parts[0])).appendingPathComponent(fileSafe(parts[1]))
        if let g = folder(for: name) { dir = dir.appendingPathComponent(g) }
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let data = try PropertyListSerialization.data(fromPropertyList: dict, format: .xml, options: 0)
        try data.write(to: dir.appendingPathComponent(fileSafe(name) + ".aupreset"))
        total += 1
    }
}
print("wrote \(total) presets for \(plugins) plugins into \(outRoot.path)")
