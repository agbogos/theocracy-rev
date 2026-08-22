# Philos Laboratories, and how Theocracy came to exist

The circumstances the game was made in, and what became of the people and the
code. Nothing here was read off the binaries.

**This doc is held to a lower standard of evidence than the rest of `docs/`, on
purpose.** Everything else in this repository is checkable — a decompile, a byte
offset, a run. This is history assembled from secondary accounts, most of which
we have not read in the original, some of which contradict each other. It is
kept separate for exactly that reason: so that nothing in it can be mistaken for
a repo-grade fact, and so that
[reference/re-methodology.md](reference/re-methodology.md)'s rule — a documented
wrong claim is worse than a missing one — is not quietly diluted by material
that cannot meet it. Where a claim rests on a source we could not retrieve, it
says so.

The companion to this doc is [story.md](story.md), which is the author's own
account of the project. This one is about Philos.

---

## Disambiguation, because search results are useless

Three unrelated games are called Theocracy. Ours is the Philos Laboratories one,
published by Ubi Soft in 2000, never re-released and on no storefront. A 2022
solo-developed 4X of the same name exists on Steam, as does *Olorun: Theocracy*.
Community pages for the 2000 game sometimes carry a "buy on Steam" link pointing
at the 2022 one, patch notes dated 2024 belong to it, and the two have separate
PCGamingWiki pages that search engines mix freely.

There is also a persistent **1999 vs 2000** confusion in databases and box art.
Both dates are defensible for different reasons — see the publishing chain
below.

## The studio

Philos Laboratories was founded in 1995 by **Zsolt Vámosi**, initially doing
graphics for advertising rather than games.

In 1997 it hired several developers from **Perihelion: The Prophecy**, the
1993 Amiga RPG, and that team became Theocracy's core. The design brief was
explicitly to **merge Command & Conquer with Civilization**, and the **Aztec
setting was chosen by the graphic artists rather than the designers** — which,
if true, is a fairly unusual origin for a game whose entire identity is its
setting. Development ran about two years toward a planned **spring 1999**
release.

That 1997 hire is the most useful fact in this document, and it is picked up
again at the bottom.

Philos went on to ship two more titles — a prison-escape game in 2001 and
*Rebels: Prison Escape* in 2003, the latter with cdv. (Our source names the 2001
title "Escape from Alcatraz"; it is more usually listed as *Alcatraz: Prison
Escape*, and we have not checked which is right.) The studio **disbanded in
2004**.

## The publishing chain, and why there are two Theocracies

This sequence matters more than it looks, because it is the only public evidence
about who owns the game today.

1. **Interactive Magic** held the publishing rights during development.
2. **March 1999** — i-Magic abandoned boxed releases for future titles, and
   Theocracy was stranded.
3. **Philos bought the publishing rights back from i-Magic.**
4. Philos spent the reprieve reworking the game: **most of the graphics were
   redrawn**, the **resolution moved to 800×600**, and the **Chronicles** —
   the eight tutorial scenarios — were **added**.
5. **Ubi Soft picked up the publishing rights**, and the game shipped.

**Step 5 was a licence, not a sale — confirmed 2026-08-15 from the retail box**,
which carries the copyright in **Philos Laboratories** and states the game is
*licensed exclusively to Ubi Soft*. This is the one claim in this document read
directly off a primary artefact rather than assembled from secondary accounts,
and it happens to answer the question everything else here was circling: Philos
kept the copyright throughout and only ever licensed publication — first to
Interactive Magic, then, after buying it back, to Ubi Soft. The consequences for
this repository are in the [legal note](README.md#legal-note).

So the game previewed in 1998–99 is materially a different product from the one
that shipped: lower resolution, different art, no Chronicles. No build or
screenshot of it is known to survive, though period magazine previews would show
the old art. It is the closest thing Theocracy has to lost content, and nobody
has collected it.

**Release date:** sources split between **24 March 2000** and **30 March 2000**,
most likely a staggered UK-versus-continental release. The game never left a
sub-1.0 version string; the final build is **v0.6.85**, shipped in English,
French, German, Italian and Hungarian.

## What shipped

Two CDs carrying **both the Windows and the Linux build**, with SafeDisc
protection on the Windows side that stopped working on Windows Vista and later.
The Linux side ships unprotected, which is [why this project runs the Linux
binaries on all three hosts](porting/other-os-ports.md).

Tux Games, which sold it, claimed at the time that Theocracy was **the first
game released with the Windows and Linux versions on the same disc**, against
the prevailing pattern of a separate Linux SKU. The claim comes from an
interested party and we have not tried to check it, but no counterexample has
been offered either.

The box promised "an amazing game engine" displaying "up to 50000 units on the
screen without any slowdown". Contemporary accounts describe it lagging on
hardware exceeding the recommended spec, and running *better under Linux than
under Windows 98 on the same machine*.

## The raid, and the sentence

In **July 2000**, four months after release, police raided the Philos offices
following a tip-off to a BSA hotline — an anonymous call, acted on without prior
evidence. The account below is from a January 2003 interview Vámosi gave to the
Hungarian technology publication HWSW, which is the primary source for all of
it.

- **21 of roughly 35 machines were seized.** About half the office hardware was
  personally owned by staff, the company having no investor at that point. The
  machines came back after two months; **the hard drives did not, because they
  had been entered as evidence.**
- **Machines running Linux were not examined at all.**
- The unlicensed software found was Windows, 3D Studio, antivirus, a file
  manager, games, and some software unrelated to game development at all,
  including architectural CAD. Damages split roughly a third Microsoft, a third
  Autodesk, a third others.
- Vámosi paid **4 million HUF** in damages, borrowed from family and friends,
  and stated that Philos had legitimately bought **35 million HUF** of software
  over the preceding years.
- In **January 2003** he was sentenced to **one year of custodial imprisonment**
  — reportedly the **first custodial sentence handed down in Hungary for
  software copyright infringement alone**, with no concurrent conviction. The
  BSA publicised it as a precedent before the judgment was final.

His own framing was that unlicensed use is fare-dodging rather than theft, and
belongs in civil rather than criminal court. He recounts the judge acknowledging
in the oral reasoning that the standard being applied could imprison enormous
numbers of people, "but you're the one sitting here."

Separately and compoundingly, a venture-capital investor withdrew from Hungary
at the moment the Philos investment was to be signed. The company spent six
months in survival mode before ABN AMRO Capital invested.

## Is there a lost source-code cache?

The seized drives make a tempting story — a Theocracy source tree sitting in a
Hungarian evidence locker. Almost certainly not, on three counts. This is
reasoning rather than reporting, and is flagged as such.

1. **The raid did not take everything.** 21 of ~35 machines went; the rest
   stayed, and staff were permitted, under supervision, to copy their data onto
   the machines that remained. Whatever the build tree looked like in July 2000,
   it existed in more than one place.
2. **The studio kept working for four more years**, shipping two more games and
   taking an institutional investment in between. A studio that had just lost
   its codebase does not do that — and Theocracy had shipped four months
   earlier, so the gold master was by definition already out of the building.
3. **Evidence retention is not archival storage.** The drives were held as
   material evidence in a case that reached first-instance judgment in 2003. In
   a copyright case the drives *are* the infringing copies, so disposal or
   wiping is the expected end state, not indefinite shelving. Vámosi's statement
   was that they would never get the drives back *because they were the
   evidence* — a statement about his loss, not a claim that a warehouse exists.

The dull answer is almost certainly the right one: the **2004 dissolution**.
That is when a studio's CD-Rs, tapes, network shares and dev machines get boxed
up, taken home, or skipped. No dramatic event — an insolvency and twenty years.

The people who would actually know are alive and have given press interviews
about precisely this period. Nobody appears to have asked them.

## The sequel that was being considered

Vámosi told Eurogamer in 2000 that Philos was looking at a follow-up using a
**different, then-undecided civilisation**, incorporating **features conceived
after Theocracy hit feature freeze** — which implies a documented backlog of cut
and deferred features existed at the studio. Nothing about its contents has been
published. We could not retrieve the interview itself and are relying on
secondary citation of it.

## Where the history explains the code

One item here is not colour — it is the answer to a question this repository
raised from the other end.

[overview.md](overview.md) argues, from the code alone, that libmvos carries
**AmigaOS heritage**: Exec-style `cList`/`cNode` intrusive lists, message ports,
the `cTask`/`cProcess` split, the general shape of the executive. That argument
was made structurally, by recognising the idioms, with no external evidence for
why an Aztec RTS written for Linux and Windows in 1997–2000 would be built that
way.

**The 1997 hire is why.** Philos staffed Theocracy's core team out of
*Perihelion: The Prophecy* — an **Amiga** title. The engine looks like AmigaOS
because it was written by people who had just spent years writing for AmigaOS,
and who carried its executive model into a new codebase rather than learning
someone else's.

This is the one place where the history and the reverse-engineering meet, and
each supports the other: the code predicted a heritage, and the studio history
supplies the mechanism. It is also a caution — the idioms in libmvos are not
evidence of a port from Amiga, only of the habits of the people who wrote it.

## What is unverified, and what is at risk

Held deliberately, so nobody later mistakes absence of doubt for verification:

- The **Eurogamer interviews** with Vámosi and Ruttmayer from March 2000 are the
  primary source behind most of the studio history above, including the sequel
  plans and the Aztec-setting anecdote. They are cited widely and we retrieved
  neither. Everything traceable to them is second-hand here.
- The **HWSW interview** is the exception: it is the direct primary source for
  the raid and the sentence, and the detail above comes from it.
- The 2001 title's name is unconfirmed (above).
- The **first-dual-platform-disc claim** originates with the retailer.
- A Hungarian podcast episode devoted to the Philos story exists and is
  untranscribed. It is likely the densest account of the studio anywhere, and
  it is audio in a language most researchers of this game do not read.
- `theocracy.com` is long dead; only 2003 Wayback captures survive.

One thing in this doc is *better* evidenced than the rest of `docs/`, not worse:
the copyright and licensing line, which was read off the retail packaging
directly. It is worth noticing that the question public sources had left open
for twenty-five years was answered by looking at the box.

The rights question — who could actually license this game today — is treated in
the [legal note](README.md#legal-note), because it bears on this repository
rather than on Philos' history.
