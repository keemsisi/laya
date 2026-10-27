#include <algorithm>
#include <atomic>
#include <thread>

#include "golden.hpp"
#include "laya/unicodedata.hpp"
#include "router_check.hpp"

using laya_test::At;
using laya_test::Value;
namespace rt = laya::router;
namespace pv = laya::pyvalue;
namespace rc = router_check;

namespace {
const char* kFile = "go_extra.json";
constexpr size_t kExpectNames = 263, kExpectCodes = 279, kExpectWorkflows = 50, kExpectRoutes = 1058,
                 kExpectBatches = 63;
}  // namespace

// The family sizes are asserted so a regenerated golden that shrinks cannot pass silently.
TEST(RouterGoldenSize, FamiliesKeepTheirSize) {
  LAYA_GOLDEN_LIST(names, kFile, "router", "names");
  EXPECT_EQ(names->size(), kExpectNames);
  EXPECT_EQ(At(laya_test::Load(kFile, true), {"router", "codes"})->size(), kExpectCodes);
  EXPECT_EQ(At(laya_test::Load(kFile, true), {"router", "workflows"})->size(), kExpectWorkflows);
  EXPECT_EQ(At(laya_test::Load(kFile, true), {"router", "routes"})->size(), kExpectRoutes);
  EXPECT_EQ(At(laya_test::Load(kFile, true), {"router", "batches"})->size(), kExpectBatches);
}

class NamesChunk : public ::testing::TestWithParam<int> {};
TEST_P(NamesChunk, MatchPython) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "names");
  LAYA_EXPECT_ROWS(rows, 263);
  size_t lo = size_t(GetParam()) * laya_test::kChunk, hi = std::min(rows->size(), lo + laya_test::kChunk);
  for (size_t i = lo; i < hi; i++) {
    const Value& rec = rows->value(i);
    const std::string name = rec.Find("name")->as_string();
    EXPECT_EQ(rt::CanonicalName(name), rec.Find("canonical")->as_string()) << name;
    laya::Result<rt::Checkpoint> got = rt::NormaliseName(name);
    if (const Value* err = rec.Find("error")) {
      ASSERT_FALSE(got.ok()) << name;
      EXPECT_EQ(got.error(), err->as_string());
    } else {
      ASSERT_TRUE(got.ok()) << name << ": " << got.error();
      EXPECT_EQ(std::string(rt::Name(got.value())), rec.Find("normalised")->as_string()) << name;
    }
    std::optional<rt::ModelSpec> spec = rt::ResolveModelSpec(name);
    const Value* want = rec.Find("spec");
    if (want->is_null()) {
      EXPECT_FALSE(spec.has_value()) << name;
    } else {
      ASSERT_TRUE(spec.has_value()) << name;
      EXPECT_EQ(spec->repo, want->items()[0].as_string()) << name;
      EXPECT_EQ(spec->subfolder, rc::Str(&want->items()[1])) << name;
    }
  }
}
INSTANTIATE_TEST_SUITE_P(Rows, NamesChunk, ::testing::ValuesIn(laya_test::Chunks(263)));

class CodesChunk : public ::testing::TestWithParam<int> {};
TEST_P(CodesChunk, MatchPython) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "codes");
  LAYA_EXPECT_ROWS(rows, 279);
  size_t lo = size_t(GetParam()) * laya_test::kChunk, hi = std::min(rows->size(), lo + laya_test::kChunk);
  for (size_t i = lo; i < hi; i++) {
    const Value& rec = rows->value(i);
    const std::string code = rec.Find("code")->as_string();
    std::optional<bool> got = rt::EnglishFromCode(code);
    const Value* want = rec.Find("english");
    if (want->is_null()) {
      EXPECT_FALSE(got.has_value()) << laya::pyjson::Repr(code);
    } else {
      ASSERT_TRUE(got.has_value()) << laya::pyjson::Repr(code);
      EXPECT_EQ(*got, want->as_bool()) << laya::pyjson::Repr(code);
    }
  }
}
INSTANTIATE_TEST_SUITE_P(Rows, CodesChunk, ::testing::ValuesIn(laya_test::Chunks(279)));

TEST(RouterWorkflowGolden, MatchPython) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "workflows");
  int matched = 0;
  for (size_t i = 0; i < rows->size(); i++) {
    const Value& rec = rows->value(i);
    std::vector<std::string> ids;
    for (const Value& id : rec.Find("ids")->items()) ids.push_back(id.as_string());
    std::string want = rc::Str(rec.Find("workflow"));
    EXPECT_EQ(rt::MatchTypedDecisionsWorkflow(ids), want) << i;
    matched += want.empty() ? 0 : 1;
  }
  EXPECT_GE(matched, 4);
}

class RoutesChunk : public ::testing::TestWithParam<int> {};
TEST_P(RoutesChunk, MatchPython) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "routes");
  LAYA_EXPECT_ROWS(rows, 1058);
  size_t lo = size_t(GetParam()) * laya_test::kChunk, hi = std::min(rows->size(), lo + laya_test::kChunk);
  for (size_t i = lo; i < hi; i++) {
    std::string diff = rc::CheckRoute(rows->value(i));
    EXPECT_EQ(diff, "") << i << " " << rc::Str(rows->value(i).Find("case"));
  }
}
INSTANTIATE_TEST_SUITE_P(Rows, RoutesChunk, ::testing::ValuesIn(laya_test::Chunks(1058)));

TEST(RouterRoutesGolden, CoversBothOutcomes) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "routes");
  int decided = 0, refused = 0, router_refused = 0;
  for (const Value& r : rows->items()) {
    if (r.Find("decision")) decided++;
    if (r.Find("error")) refused++;
    if (r.Find("router_error")) router_refused++;
  }
  EXPECT_GE(decided, 900);
  EXPECT_GE(refused, 100);
  EXPECT_GE(router_refused, 4);
}

class BatchesChunk : public ::testing::TestWithParam<int> {};
TEST_P(BatchesChunk, MatchPython) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "batches");
  size_t lo = size_t(GetParam()) * 20, hi = std::min(rows->size(), lo + 20);
  for (size_t i = lo; i < hi; i++) {
    const Value& rec = rows->value(i);
    laya::Result<rt::Router> r = rt::Router::Create(rc::OptionsOf(*rec.Find("router")));
    ASSERT_TRUE(r.ok()) << r.error();
    std::vector<rt::Request> reqs;
    for (const Value& item : rec.Find("requests")->items()) reqs.push_back(rc::RequestOf(item));
    laya::Result<std::vector<rt::Decision>> got = r.value().RouteBatch(reqs);
    if (const Value* err = rec.Find("error")) {
      ASSERT_FALSE(got.ok()) << i;
      EXPECT_EQ(got.error(), err->as_string()) << i;
      continue;
    }
    ASSERT_TRUE(got.ok()) << i << ": " << got.error();
    const Value& want = *rec.Find("decisions");
    ASSERT_EQ(got.value().size(), want.size()) << i;
    for (size_t j = 0; j < want.size(); j++) EXPECT_EQ(rc::Diff(got.value()[j], want.items()[j]), "") << i << "/" << j;
  }
}
INSTANTIATE_TEST_SUITE_P(Rows, BatchesChunk, ::testing::Values(0, 1, 2, 3));

TEST(RouterBatchGolden, SomeBatchesFail) {
  LAYA_GOLDEN_LIST(rows, kFile, "router", "batches");
  int failed = 0;
  for (const Value& r : rows->items()) failed += r.Find("error") ? 1 : 0;
  EXPECT_GE(failed, 3);
}

// ---- tests that need no golden ---------------------------------------------------------------

TEST(Router, CheckpointIsClosed) {
  ASSERT_EQ(rt::Checkpoints().size(), 3u);
  EXPECT_EQ(rt::Name(rt::Checkpoints()[0]), "english");
  EXPECT_EQ(rt::Name(rt::Checkpoints()[1]), "multilingual");
  EXPECT_EQ(rt::Name(rt::Checkpoints()[2]), "typed-decisions");
  EXPECT_FALSE(rt::NormaliseName("gpt").ok());
}

TEST(Router, DefaultIsMultilingualAndLoadsNothing) {
  auto r = rt::Router::Create();
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value().Default(), rt::Checkpoint::Multilingual);
  EXPECT_EQ(r.value().Spec(rt::Checkpoint::English).String(), "convaiinnovations/laya");
  EXPECT_EQ(r.value().Spec(rt::Checkpoint::Multilingual).String(), "convaiinnovations/laya/multilingual");
}

TEST(Router, WithDefaultAndStandalone) {
  rt::Options o;
  o.default_model = " EN ";
  o.standalone_repos = true;
  auto r = rt::Router::Create(o);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value().Default(), rt::Checkpoint::English);
  auto d = r.value().Route(pv::Value::String("12345"));
  ASSERT_TRUE(d.ok());
  EXPECT_EQ(d.value().model, rt::Checkpoint::English);
  EXPECT_EQ(d.value().repo, "convaiinnovations/laya");
  EXPECT_EQ(d.value().reason, "no letters detected in state; using default (english)");
  ASSERT_TRUE(d.value().detection.has_value());
  EXPECT_EQ(d.value().Repr(), "RouteDecision(model='english', reason='no letters detected in state; using default (english)')");
}

TEST(Router, UnknownDefaultAndSourceAreRefused) {
  rt::Options o;
  o.default_model = "bogus";
  auto r = rt::Router::Create(o);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.error().rfind("unknown model 'bogus'; choose one of ['english', 'multilingual', 'typed-decisions']", 0), 0u);
  rt::Options s;
  s.WithSource("nope", "x");
  EXPECT_FALSE(rt::Router::Create(s).ok());
}

TEST(Router, SourceOverridesAndTheLastWins) {
  rt::Options o;
  o.WithSource("en", "/m/one").WithSourceIn("english", "org/repo", "sub");
  auto r = rt::Router::Create(o);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value().Spec(rt::Checkpoint::English).String(), "org/repo/sub");
}

TEST(Router, ExpandUserFollowsHomeAndLeavesTheRest) {
  ASSERT_EQ(setenv("HOME", "/home/x/", 1), 0);
  rt::Options o;
  o.WithSource("en", "~/m").WithSource("ml", "~").WithSource("typed", "a/~b");
  auto r = rt::Router::Create(o);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value().Spec(rt::Checkpoint::English).repo, "/home/x/m");
  EXPECT_EQ(r.value().Spec(rt::Checkpoint::Multilingual).repo, "/home/x");
  EXPECT_EQ(r.value().Spec(rt::Checkpoint::TypedDecisions).repo, "a/~b");
  rt::Options u;
  u.WithSource("en", "~no-such-account-laya/x").WithSourceIn("ml", "~/kept", "s");
  auto r2 = rt::Router::Create(u);
  ASSERT_TRUE(r2.ok());
  EXPECT_EQ(r2.value().Spec(rt::Checkpoint::English).repo, "~no-such-account-laya/x");
  EXPECT_EQ(r2.value().Spec(rt::Checkpoint::Multilingual).repo, "~/kept");  // WithSourceIn does not expand
  ASSERT_EQ(setenv("HOME", "/", 1), 0);
  rt::Options root;
  root.WithSource("en", "~");
  EXPECT_EQ(rt::Router::Create(root).value().Spec(rt::Checkpoint::English).repo, "/");
}

TEST(Router, PerCallHintBeatsRouterHintAndAbstentionFallsThrough) {
  rt::Options o;
  o.lang_guess = rt::LangCode("de");
  auto r = rt::Router::Create(o);
  ASSERT_TRUE(r.ok());
  rt::CallOptions c;
  c.lang_guess = rt::LangCode("en");
  auto d = r.value().Route(pv::Value::String("hola"), {}, c);
  ASSERT_TRUE(d.ok());
  EXPECT_EQ(d.value().reason, "lang_guess: the caller identified this as English text");
  c.lang_guess = rt::LangCode("C");  // abstains, so the Router's own hint answers
  d = r.value().Route(pv::Value::String("hola"), {}, c);
  EXPECT_EQ(d.value().reason, "Router(lang_guess=...): the caller identified this as non-English text");
  EXPECT_FALSE(d.value().detection.has_value());
}

TEST(Router, HintSeesTheState) {
  rt::CallOptions c;
  c.lang_guess = [](const pv::Value& s) { return s.kind() == pv::Value::Kind::String ? s.as_string() : std::string(); };
  auto r = rt::Router::Create();
  EXPECT_EQ(r.value().Route(pv::Value::String("fr-FR"), {}, c).value().model, rt::Checkpoint::Multilingual);
  EXPECT_EQ(r.value().Route(pv::Value::String("en_GB"), {}, c).value().model, rt::Checkpoint::English);
}

TEST(Router, WorkflowIsReportedEvenWhenAutoIsOff) {
  auto r = rt::Router::Create();
  std::vector<std::string> ids = {"urgency", "action", "category", "churn_risk", "needs_human"};
  auto d = r.value().Route(pv::Value::String("hello"), ids);
  EXPECT_EQ(d.value().workflow, "customer_service");
  EXPECT_NE(d.value().model, rt::Checkpoint::TypedDecisions);
  rt::Options o;
  o.auto_task_detection = true;
  d = rt::Router::Create(o).value().Route(pv::Value::String("hello"), ids);
  EXPECT_EQ(d.value().model, rt::Checkpoint::TypedDecisions);
  EXPECT_EQ(d.value().reason, "question ids match the 'customer_service' typed-decisions workflow");
  ids.push_back("extra");
  EXPECT_EQ(rt::MatchTypedDecisionsWorkflow(ids), "");
  ids.pop_back();
  ids.push_back("action");  // a repeated id is the same set
  EXPECT_EQ(rt::MatchTypedDecisionsWorkflow(ids), "customer_service");
}

TEST(Router, TaskSpellings) {
  auto r = rt::Router::Create();
  for (const char* t : {"typed_decisions", "Typed-Decisions", "TYPED-DECISIONS", "typed", "decisions"}) {
    rt::CallOptions c;
    c.task = t;
    auto d = r.value().Route(pv::Value::String("x"), {}, c);
    ASSERT_TRUE(d.ok()) << t;
    EXPECT_EQ(d.value().model, rt::Checkpoint::TypedDecisions) << t;
    EXPECT_EQ(d.value().reason, std::string("explicit task=") + laya::pyjson::Repr(t));
  }
  rt::CallOptions bad;
  bad.task = "summarise";
  EXPECT_FALSE(r.value().Route(pv::Value::String("x"), {}, bad).ok());
}

TEST(Router, BatchKeepsOrderAndRefusesNullState) {
  auto r = rt::Router::Create();
  std::vector<rt::Request> reqs(3);
  reqs[0].state = pv::Value::String("The invoice is overdue and needs a reply today.");
  reqs[1].state = pv::Value::String("Привет, как дела у вас сегодня?");
  reqs[2].state = pv::Value::String("x");
  reqs[2].options.model = "typed";
  auto d = r.value().RouteBatch(reqs);
  ASSERT_TRUE(d.ok());
  ASSERT_EQ(d.value().size(), 3u);
  EXPECT_EQ(d.value()[0].model, rt::Checkpoint::English);
  EXPECT_EQ(d.value()[1].model, rt::Checkpoint::Multilingual);
  EXPECT_EQ(d.value()[2].model, rt::Checkpoint::TypedDecisions);
  reqs[1].state = pv::Value::Null();
  auto bad = r.value().RouteBatch(reqs);
  ASSERT_FALSE(bad.ok());
  EXPECT_EQ(bad.error(), "request 1 'state' must not be None; pass a string, dict, or list");
  EXPECT_TRUE(r.value().RouteBatch({}).ok());
}

TEST(Router, TheCutIsSixtyCodePointsNotBytes) {
  // Recorded from Python: the mixed German line is 167 code points long and the reason quotes the
  // first 60, which hold one two-byte letter, so a byte cut would end a character earlier.
  std::string english;
  for (int i = 0; i < 12; i++) english += "Please review the error and reply to the customer today. ";
  const std::string german =
      "Wie sp\xc3\xa4t ist es heute in K\xc3\xb6ln und wann kommt der Zug an, bitte sag mir das schnell. "
      "Wie sp\xc3\xa4t ist es heute in K\xc3\xb6ln und wann kommt der Zug an, bitte sag mir das schnell. ";
  pv::Value state = pv::Value::Array({pv::Value::String(english), pv::Value::String(german)});
  auto d = rt::Router::Create().value().Route(state);
  ASSERT_TRUE(d.ok());
  EXPECT_EQ(d.value().reason,
            "Latin script, mostly English, but a line or field reads as 'de' "
            "('Wie sp\xc3\xa4t ist es heute in K\xc3\xb6ln und wann kommt der Zug an, bit'); "
            "the English checkpoint cannot read it");
}

TEST(Router, ConcurrentRoutingIsSafe) {
  rt::Options o;
  std::atomic<int> calls{0};
  o.lang_guess = [&calls](const pv::Value&) {
    calls++;
    return std::string("C");  // abstains, so detection runs too
  };
  auto r = rt::Router::Create(o);
  ASSERT_TRUE(r.ok());
  const rt::Router& router = r.value();
  std::atomic<int> bad{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; t++)
    ts.emplace_back([&, t] {
      for (int i = 0; i < 200; i++) {
        bool en = (i + t) % 2 == 0;
        auto d = router.Route(pv::Value::String(en ? "Please review the error and reply to the customer." : "Привет, как дела?"));
        if (!d.ok() || d.value().model != (en ? rt::Checkpoint::English : rt::Checkpoint::Multilingual)) bad++;
      }
    });
  for (auto& t : ts) t.join();
  EXPECT_EQ(bad.load(), 0);
  EXPECT_EQ(calls.load(), 1600);
}
