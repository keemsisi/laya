// laya-ts/tests/identifier-complexity.test.ts
//
// Regression: stripping identifiers must stay linear in the length of a word-character run.
//
// IDENTIFIER_RE removes dot- and @-joined tokens before words are counted. Without the
// leading lookbehind the greedy prefix is retried at every offset inside a run of word
// characters, and each attempt rescans the run before failing on the absent [.@] --
// quadratic in the run's length. Measured on node 20: 50 000 characters of one token took
// 1540 ms, against 0.05 ms for ordinary prose of the same size. `analyse` runs on the
// default routing path once per request, so the cost is reachable by any caller.
//
// The lookbehind removes no match: the word class and [.@] are disjoint, so an attempt
// from inside a run consumes to exactly the same separator as one from the run's start.
// Parity below is asserted against the previous pattern rather than typed expectations.
import { describe, expect, it } from "vitest";
import { analyse, latinProfile } from "../src/lang.js";

const PREVIOUS = /[\p{L}\p{N}_-]*(?:[.@][\p{L}\p{N}_-]+)+/gu;
const CURRENT = /(?<![\p{L}\p{N}_-])[\p{L}\p{N}_-]*(?:[.@][\p{L}\p{N}_-]+)+/gu;

const strip = (re: RegExp, s: string) => s.replace(new RegExp(re.source, re.flags), " ");
const elapsed = (fn: () => void) => {
  const started = performance.now();
  fn();
  return performance.now() - started;
};

describe("identifier stripping: parity with the previous pattern", () => {
  const cases = [
    "github.com", "user@acme.com", "v1.2.3", "U.S.A.", "arrivato.", "a.b", "a@b",
    "sub.domain.co.uk", "first.last@sub.example.org", "192.168.0.1", "-a.b-", "_x.y_",
    "foo..bar", ".com", "a.", "@a", "a@", "e.g.", "Ü.Ö", "naïve.café", "a-b.c-d",
    "x-a.b", "9.9", "a..b", "a.-b", "a-.b-.c", "", ".", "@",
    "Contact support@acme.com or github.com/acme for v2.10.1 details.",
    "No identifiers here at all just words",
    // A local part at RFC 5321's 64-character limit, and a run past any plausible bound:
    // a length-bounded fix strips only part of these and leaves the rest as a word.
    "a".repeat(64) + "@example.com",
    "a".repeat(200) + ".example.com",
    "grazie mille per " + "wzqxk".repeat(15) + ".example.com",
  ];
  for (const token of cases) {
    it(`strips ${JSON.stringify(token.slice(0, 34))} identically`, () => {
      expect(strip(CURRENT, token)).toBe(strip(PREVIOUS, token));
    });
  }

  it("agrees with the previous pattern on 20 000 random strings", () => {
    let seed = 12345;
    // `Math.imul`, not `seed * 1103515245`: that product reaches 2^61, past the 2^53 a double
    // holds exactly, so its low bits round away and the generator collapses into a cycle of
    // 10 466 states -- the 20 000 iterations below then cover 540 distinct strings instead of
    // 19 004. Dividing by 2^32 also closes the old `/ 0x7fffffff`, which returns exactly 1.0 at
    // state 0x7fffffff and would index one past the alphabet. Same form as bpe-complexity.test.ts.
    const rnd = () => ((seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296);
    const alphabets = ["aA1._@- ", "._@-", "áéÜß._@-", "abcXYZ019._@- -_"];
    let mismatch: string | null = null;
    const seen = new Set<string>();
    for (let i = 0; i < 20_000 && mismatch === null; i++) {
      const alphabet = alphabets[i % alphabets.length]!;
      const len = Math.floor(rnd() * 60);
      let s = "";
      for (let j = 0; j < len; j++) s += alphabet[Math.floor(rnd() * alphabet.length)];
      seen.add(s);
      if (strip(CURRENT, s) !== strip(PREVIOUS, s)) mismatch = s;
    }
    expect(mismatch).toBeNull();
    // Pin the coverage the assertion above depends on, so a generator that collapses again is a
    // failure rather than a quietly smaller differential test. 19 004 here, 540 before the fix.
    expect(seen.size).toBeGreaterThan(15_000);
  });

  it("leaves detection of ordinary prose unchanged", () => {
    expect(analyse("The customer was billed twice and wants a refund").isEnglish).toBe(true);
  });
});

describe("identifier stripping: complexity", () => {
  // These go through latinProfile / analyse -- the exported functions that use the
  // shipped IDENTIFIER_RE -- so reverting src/lang.ts fails them. Testing the pattern
  // literal above would only ever test the literal.
  //
  // Ceilings are set to fail the previous pattern, not merely to be generous. Previous
  // cost / ceiling / current cost on node 20, via latinProfile:
  //   20 000 chars    250 ms / 60 ms  / 0.1 ms
  //   50 000 chars  1 540 ms / 200 ms / 0.3 ms
  it("latinProfile on 20 000 characters of one token is bounded", () => {
    expect(elapsed(() => latinProfile("a".repeat(20_000)))).toBeLessThan(60);
  });

  it("latinProfile on 50 000 characters of one token is bounded", () => {
    expect(elapsed(() => latinProfile("a".repeat(50_000)))).toBeLessThan(200);
  });

  it("a single trailing separator does not reopen the quadratic path", () => {
    expect(elapsed(() => latinProfile("a".repeat(50_000) + "."))).toBeLessThan(200);
  });

  it("hyphen and underscore runs are linear too", () => {
    expect(elapsed(() => latinProfile("a-".repeat(25_000)))).toBeLessThan(200);
    expect(elapsed(() => latinProfile("a_".repeat(25_000)))).toBeLessThan(200);
  });

  // No assertion at the 4000 characters `stateText` caps detection to: `analyse` does
  // enough non-regex work at that size that quadratic and linear are only a few-fold
  // apart, which CI jitter covers either way. The 20 000 and 50 000 character cases
  // above carry the guarantee -- they are 250x and 1500x apart.

  // The scaling claim, asserted against the PREVIOUS pattern rather than against this one's own
  // timing at another size.
  //
  // A self-ratio cannot be made reliable here. It was warmed up and taking the best of five and it
  // still failed CI at 11.33x against a bar of 8 (`TypeScript (node22)`, and on main, not on any
  // one PR). Two reasons, and neither is fixable by more repetitions: the 12 500-character baseline
  // is a fraction of a millisecond, where `performance.now()` granularity is a large share of the
  // reading; and best-of-N only survives OCCASIONAL interference, while a shared runner under
  // sustained contention inflates every run of the larger size together. Measured on an idle
  // machine the same ratio is 4.34x, 4.11x and 4.41x at three size pairs -- the algorithm was never
  // the problem, the instrument was.
  //
  // The cross-pattern gap has no such margin problem. Both patterns run on the SAME string in the
  // same process, so contention hits both, and the separation is enormous: at 12 500 characters of
  // one word-character run, measured 0.060 ms against 97.4 ms -- 1 619x. The bar below is 100x, so
  // there is more than an order of magnitude of headroom. Quadratic growth is what produces that
  // gap and it widens with size (250x at 4 000 characters, 1 033x at 8 000, 3 289x at 25 000), so
  // no runner can make the two swap places.
  //
  // 12 500 characters rather than 50 000 keeps the previous pattern at about 100 ms instead of
  // 1.6 s, which is the whole cost this check adds.
  it("the lookbehind is what makes the pattern linear (previous is >100x slower)", () => {
    const s = "a".repeat(12_500);
    const bestOf = (re: RegExp, runs: number) => {
      strip(re, s);                       // once untimed, so JIT tiering is charged to neither
      return Math.min(...Array.from({ length: runs }, () => elapsed(() => strip(re, s))));
    };
    const current = bestOf(CURRENT, 5);
    const previous = bestOf(PREVIOUS, 2);
    expect(previous / current).toBeGreaterThan(100);
  });

  // And the shipped path, in absolute milliseconds. These are what actually guard `analyse`, and
  // they hold by a wide margin either way: with the previous pattern restored, 20 000 characters
  // takes about 250 ms against the 60 ms ceiling asserted above and 50 000 takes about 1 600 ms
  // against 200 ms. A ceiling exceeded four- to eight-fold does not need a ratio to be readable.
  it("the shipped path stays linear enough for the ceilings above to be met with room", () => {
    const best = (n: number) => {
      const s = "a".repeat(n);
      latinProfile(s);
      return Math.min(...[0, 1, 2, 3, 4].map(() => elapsed(() => latinProfile(s))));
    };
    expect(best(50_000)).toBeLessThan(200);
    expect(best(12_500)).toBeLessThan(50);
  });
});
