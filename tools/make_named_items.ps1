# Writes src/named_items.h: the game's unique and set items as the menu lists them.
#
#   make_named_items.ps1 -Tables <folder> [-Out <src\named_items.h>]
#
# <folder> holds the game's own tables, extracted from its data (global\excel\ and local\lng\strings\), under any
# file names that end in: uniqueitems.txt, setitems.txt, sets.txt, weapons.txt, armor.txt, misc.txt, itemtypes.txt,
# item-names.json. Nothing of them but the items' names, numbers and base codes goes into the header.
#
# An item's number is its row in the game's compiled table - what an item on the ground carries as its "file
# index". In the text table that is the row's place among all rows but the "Expansion" line (the table's own *ID
# column says the same; this script checks that it does).
param([Parameter(Mandatory = $true)][string]$Tables, [string]$Out = '')
$ErrorActionPreference = 'Stop'
if (-not $Out) { $Out = Join-Path (Split-Path -Parent $PSScriptRoot) 'src\named_items.h' }
$latin = [Text.Encoding]::GetEncoding(28591)
function Find-File([string]$suffix) {
  $f = @(Get-ChildItem -LiteralPath $Tables -File -Recurse | Where-Object { $_.Name.ToLower().EndsWith($suffix) -and $_.Name -notmatch 'base' } | Sort-Object { $_.Name.Length })
  if (-not $f.Count) { throw "no file ending in $suffix under $Tables" }
  return $f[0].FullName
}
function Read-Table([string]$suffix) {
  $lines = [IO.File]::ReadAllLines((Find-File $suffix), $latin)
  $h = $lines[0].Split("`t"); $rows = New-Object System.Collections.Generic.List[object]
  for ($i = 1; $i -lt $lines.Length; $i++) {
    if (-not $lines[$i]) { continue }
    $c = $lines[$i].Split("`t"); $o = @{}
    for ($k = 0; $k -lt $h.Length; $k++) { if (-not $o.ContainsKey($h[$k])) { $o[$h[$k]] = if ($k -lt $c.Length) { $c[$k] } else { '' } } }
    $rows.Add($o)
  }
  return , $rows
}
# The game's English names by string key.
$english = @{}
foreach ($e in ([IO.File]::ReadAllText((Find-File 'item-names.json'), [Text.Encoding]::UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json)) { if ($e.Key -and -not $english.ContainsKey($e.Key)) { $english[$e.Key] = [string]$e.enUS } }
function Name-Of([string]$key) {
  $n = if ($english.ContainsKey($key)) { $english[$key] } else { $key }
  $n = ($n -replace '\xFFc.', '') -replace '\[fs\]|\[ms\]|\[nl\]|\[pl\]', ''   # colour marks and the gender tags of other languages
  return $n.Trim()
}
function Lit([string]$s) {
  if ($s -match '[^\x20-\x7E]') { throw "a name with a character outside ASCII: $s" }
  return '"' + $s.Replace('\', '\\').Replace('"', '\"') + '"'
}

# A base item's type, and the group the menu lists a unique item under.
$baseType = @{}
foreach ($t in 'weapons.txt', 'armor.txt', 'misc.txt') { foreach ($r in (Read-Table $t)) { if ($r['code']) { $baseType[$r['code']] = $r['type'] } } }
$groups = @(
  @{ name = 'Helms'; types = 'helm', 'circ', 'pelt', 'phlm' }
  @{ name = 'Body armor'; types = , 'tors' }
  @{ name = 'Shields'; types = 'shie', 'ashd', 'head' }
  @{ name = 'Gloves'; types = , 'glov' }
  @{ name = 'Boots'; types = , 'boot' }
  @{ name = 'Belts'; types = , 'belt' }
  @{ name = 'Rings'; types = , 'ring' }
  @{ name = 'Amulets'; types = , 'amul' }
  @{ name = 'Charms'; types = 'scha', 'mcha', 'lcha', 'csch' }
  @{ name = 'Jewels'; types = 'jewl', 'cjwl' }
  @{ name = 'Axes'; types = 'axe', 'taxe' }
  @{ name = 'Maces, hammers and scepters'; types = 'club', 'mace', 'hamm', 'scep' }
  @{ name = 'Swords'; types = , 'swor' }
  @{ name = 'Daggers'; types = 'knif', 'tkni' }
  @{ name = 'Spears and javelins'; types = 'spea', 'aspe', 'jave', 'ajav' }
  @{ name = 'Polearms'; types = , 'pole' }
  @{ name = 'Bows and crossbows'; types = 'bow', 'abow', 'xbow' }
  @{ name = 'Staves, wands, orbs and grimoires'; types = 'staf', 'wand', 'orb', 'grim' }
  @{ name = 'Claws'; types = 'h2h', 'h2h2' }
  @{ name = 'Other'; types = @() }
)
function Group-Of([string]$code) {
  $type = $baseType[$code]
  for ($g = 0; $g -lt $groups.Count; $g++) { if ($groups[$g].types -contains $type) { return $g } }
  return $groups.Count - 1
}

# The unique items that can drop.
$uniques = New-Object System.Collections.Generic.List[object]
$place = 0; $maxId = -1
foreach ($r in (Read-Table 'uniqueitems.txt')) {
  if ($r['index'] -eq 'Expansion') { continue }
  if ($r['*ID'] -ne '' -and [int]$r['*ID'] -ne $place) { throw "uniqueitems: '$($r['index'])' has *ID $($r['*ID']) at place $place" }
  if ($r['code'] -and $r['spawnable'] -eq '1' -and $r['disabled'] -ne '1') {
    $uniques.Add(@{ id = $place; key = $r['index']; name = (Name-Of $r['index']); base = $r['code']; group = (Group-Of $r['code']) })
    $maxId = $place
  }
  $place++
}
$uniqueRows = $place
# The set items, and their sets in the order the items name them.
$setNames = New-Object System.Collections.Generic.List[string]
$setItems = New-Object System.Collections.Generic.List[object]
$place = 0
foreach ($r in (Read-Table 'setitems.txt')) {
  if ($r['index'] -eq 'Expansion') { continue }
  if ($r['*ID'] -ne '' -and [int]$r['*ID'] -ne $place) { throw "setitems: '$($r['index'])' has *ID $($r['*ID']) at place $place" }
  if ($r['item'] -and $r['set'] -and $r['disabled'] -ne '1') {
    if (-not $setNames.Contains($r['set'])) { $setNames.Add($r['set']) }
    $setItems.Add(@{ id = $place; key = $r['index']; name = (Name-Of $r['index']); base = $r['item']; group = $setNames.IndexOf($r['set']) })
  }
  $place++
}
$setRows = $place
$setKey = @{}; foreach ($r in (Read-Table 'sets.txt')) { if ($r['index']) { $setKey[$r['index']] = $r['name'] } }

$o = New-Object System.Collections.Generic.List[string]
$o.Add('// Generated by tools/make_named_items.ps1 from the game''s own tables - do not edit; run the script again after a game')
$o.Add('// update. The unique items that can drop and the set items, as the menu lists them: an item''s number (its row in')
$o.Add('// the game''s compiled UniqueItems or SetItems table: what an item on the ground carries as its file index), the')
$o.Add('// group the menu lists it under, its base item''s code, the key of its name in the game''s strings, and that name in')
$o.Add('// English for where the game''s strings are not at hand.')
$o.Add('#pragma once')
$o.Add('')
$o.Add('#include <cstdint>')
$o.Add('')
$o.Add('namespace d2rcc::named {')
$o.Add('')
$o.Add('struct Item {')
$o.Add('  uint16_t id;')
$o.Add('  uint8_t group;')
$o.Add('  char base[4];')
$o.Add('  const char* key;')
$o.Add('  const char* english;')
$o.Add('};')
$o.Add('')
$o.Add("constexpr int kUniqueRows = $uniqueRows;  // rows of the game's table (the highest number is one less)")
$o.Add("constexpr int kSetItemRows = $setRows;")
$o.Add('')
$o.Add('// The groups of the unique items, in the menu''s order.')
$o.Add('constexpr const char* kUniqueGroups[] = {')
foreach ($g in $groups) { $o.Add('    ' + (Lit $g.name) + ',') }
$o.Add('};')
$o.Add("constexpr int kUniqueGroupCount = $($groups.Count);")
$o.Add('')
$o.Add("constexpr int kUniqueCount = $($uniques.Count);")
$o.Add('constexpr Item kUniques[kUniqueCount] = {')
foreach ($u in $uniques) { $o.Add(('    {{{0}, {1}, {2}, {3}, {4}}},' -f $u.id, $u.group, (Lit $u.base), (Lit $u.key), (Lit $u.name))) }
$o.Add('};')
$o.Add('')
$o.Add('// The sets (a set item''s group), each by the key of its name and its English name.')
$o.Add("constexpr int kSetCount = $($setNames.Count);")
$o.Add('constexpr const char* kSetKeys[kSetCount] = {')
foreach ($n in $setNames) { $k = if ($setKey.ContainsKey($n)) { $setKey[$n] } else { $n }; $o.Add('    ' + (Lit $k) + ',') }
$o.Add('};')
$o.Add('constexpr const char* kSetEnglish[kSetCount] = {')
foreach ($n in $setNames) { $k = if ($setKey.ContainsKey($n)) { $setKey[$n] } else { $n }; $o.Add('    ' + (Lit (Name-Of $k)) + ',') }
$o.Add('};')
$o.Add('')
$o.Add("constexpr int kSetItemCount = $($setItems.Count);")
$o.Add('constexpr Item kSetItems[kSetItemCount] = {')
foreach ($u in $setItems) { $o.Add(('    {{{0}, {1}, {2}, {3}, {4}}},' -f $u.id, $u.group, (Lit $u.base), (Lit $u.key), (Lit $u.name))) }
$o.Add('};')
$o.Add('')
$o.Add('}  // namespace d2rcc::named')
[IO.File]::WriteAllText($Out, ($o -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))
Write-Host ("{0}: {1} unique items in {2} groups (table rows {3}), {4} set items in {5} sets (table rows {6})" -f (Split-Path -Leaf $Out), $uniques.Count, $groups.Count, $uniqueRows, $setItems.Count, $setNames.Count, $setRows)
$other = @($uniques | Where-Object { $_.group -eq $groups.Count - 1 })
if ($other.Count) { Write-Host ("in 'Other': " + (($other | ForEach-Object { "$($_.name) [$($_.base): $($baseType[$_.base])]" }) -join ', ')) -ForegroundColor Yellow }
$renamed = @($uniques + $setItems | Where-Object { $_.name -ne $_.key })
Write-Host ("names that differ from their key: {0} (e.g. {1})" -f $renamed.Count, (($renamed | Select-Object -First 4 | ForEach-Object { "'$($_.key)' -> '$($_.name)'" }) -join ', '))
