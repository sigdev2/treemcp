#include "Session.hpp"
#include "SearchProfile.hpp"

#include "Tools.hpp"

#include <QJsonArray>

#include "treemcp/Version.hpp"

namespace treemcp {
namespace {

constexpr qsizetype MAX_SESSION_REQUESTS = 16384;

} // namespace

Session::Session(const Workspace& searchWorkspace, int workers, ReplyWriter writer) :
        workspace(searchWorkspace),
        maxRequests(std::max(32, 4 * workers) + workers),
        publish(std::move(writer)),
        scheduler(workers)
{
    deadlines.setSingleShot(true);
    deadlines.setTimerType(Qt::PreciseTimer);
    connect(&deadlines, &QTimer::timeout, this, &Session::expire);
}

Session::~Session()
{
    shutdown();
}

void Session::shutdown()
{
    if (closed) {
        return;
    }
    closed = true;
    deadlines.stop();
    {
        std::lock_guard lock(completionMutex);
        for (const auto& work : std::as_const(active)) {
            if (!work->reply) {
                work->cancelled.store(true);
            }
        }
    }
    for (auto it = active.cbegin(); it != active.cend(); ++it) {
        scheduler.remove(it.key());
        if (it.value()->coordinator) {
            scheduler.cancelFiles(it.value()->files);
        }
    }
    for (auto* coordinator : { &batchCoordinator, &structuralCoordinator }) {
        if (coordinator->joinable()) {
            coordinator->join();
        }
    }
    scheduler.shutdown();
    for (const auto& id : active.keys()) {
        complete(id);
    }
}

void Session::forget(const QJsonValue& id)
{
    if (const auto work = active.take(id)) {
        admittedCost -= work->cost;
    }
    if (active.isEmpty()) {
        deadlines.stop();
    }
}

void Session::cancel(const QJsonValue& id)
{
    const auto work = active.value(id);
    if (!work) {
        return;
    }
#ifdef TREEMCP_SEARCH_PROFILE
    const auto cancelledAt = Clock::now();
#endif
    work->cancelled.store(true);
    if (work->coordinator) {
        scheduler.cancelFiles(work->files);
    } else if (scheduler.remove(id)) {
        forget(id);
#ifdef TREEMCP_SEARCH_PROFILE
        SearchProfile::event(id, "released", Clock::now());
#endif
    }
#ifdef TREEMCP_SEARCH_PROFILE
    SearchProfile::event(id, "cancel", cancelledAt);
#endif
}

void Session::complete(const QJsonValue& id)
{
    const auto work = active.value(id);
    if (!work) {
        return;
    }
    std::optional<QJsonObject> reply;
    {
        std::lock_guard lock(completionMutex);
        reply = std::move(work->reply);
    }
    if (work->coordinator && work->coordinator->joinable()) {
        work->coordinator->join();
        scheduler.reserveWorkerForSingles(false);
    }
    forget(id);
    if (!work->cancelled.load() && reply) {
        publish(Clock::now() >= work->deadline ? Tools::deadlineReply(id, *reply) : *reply);
    }
}

void Session::expire()
{
    const auto now = Clock::now();
    auto next = Clock::time_point::max();
    for (const auto& id : active.keys()) {
        const auto work = active.value(id);
        if (work->cancelled.load()) {
            continue;
        }
        if (now < work->deadline) {
            next = std::min(next, work->deadline);
            continue;
        }
        if (work->coordinator) {
            scheduler.cancelFiles(work->files);
        } else if (scheduler.remove(id)) {
            forget(id);
            publish(Tools::deadlineReply(id));
        }
    }
    if (next != Clock::time_point::max()) {
        deadlines.start(std::chrono::ceil<std::chrono::milliseconds>(next - now));
    }
}

std::optional<QJsonObject> Session::schedule(
    const QJsonValue& id,
    const QString& name,
    const QJsonObject& args,
    Clock::time_point received,
    qsizetype frameBytes)
{
    const bool batch = name == "batch_search";
    const auto deadline = received + std::chrono::milliseconds(args.value("deadlineMs").toInt());
    if (!batch && Clock::now() >= deadline) {
        return Tools::deadlineReply(id);
    }
    // Charge retained input, recursive scope and batch fan-out before capturing the request.
    const int cost = 1 + static_cast<int>(frameBytes / 65536) + (args.contains("paths") ? 3 : 0) +
        static_cast<int>(args.value("queries").toArray().size());
    if (closed || active.size() >= maxRequests || cost + admittedCost > 4 * maxRequests ||
        (batch && batchCoordinator.joinable()))
    {
        return JsonRpc::error(id, -32001, QStringLiteral("Search scheduler at capacity"));
    }
    auto work = std::make_shared<Work>();
    work->deadline = deadline;
    work->cost = cost;
    if (!publish) {
        return Tools::execute(id, name, args, workspace, { work->cancelled, deadline }, scheduler, &work->files);
    }
    active.insert(id, work);
    admittedCost += cost;
    const auto remaining = std::max(
        std::chrono::milliseconds::zero(),
        std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()));
    if (!deadlines.isActive() || remaining < std::chrono::milliseconds(deadlines.remainingTime())) {
        deadlines.start(remaining);
    }
    auto run = [this, id, name, args, work] {
        auto reply = Tools::execute(
            id,
            name,
            args,
            workspace,
            { work->cancelled, work->deadline },
            scheduler,
            work->coordinator ? &work->files : nullptr);
        {
            std::lock_guard lock(completionMutex);
            work->reply = std::move(reply);
        }
        QMetaObject::invokeMethod(
            this,
            [this, id] { complete(id); },
            Qt::QueuedConnection);
    };
    if (batch) {
        work->coordinator = &batchCoordinator;
    } else if (
        name == "structural_search" && args.contains("paths") && scheduler.workerCount() > 1 &&
        !structuralCoordinator.joinable())
    {
        work->coordinator = &structuralCoordinator;
    }
    if (work->coordinator) {
        const bool competing = batch ? structuralCoordinator.joinable() : batchCoordinator.joinable();
        if (competing) {
            scheduler.reserveWorkerForSingles(true);
        }
        try {
            *work->coordinator = std::thread(run);
            return std::nullopt;
        } catch (const std::system_error&) {
            if (competing) {
                scheduler.reserveWorkerForSingles(false);
            }
            if (batch) {
                forget(id);
                return JsonRpc::error(id, -32001, QStringLiteral("Batch coordinator unavailable"));
            }
            work->coordinator = nullptr;
        }
    }
    scheduler.submit(id, std::move(run));
    return std::nullopt;
}

std::optional<QJsonObject> Session::handle(QByteArrayView frame)
{
    const auto received = Clock::now();
    const auto decoded = JsonRpc::decode(frame);
    if (const auto* request = std::get_if<JsonRpc::Request>(&decoded)) {
        return dispatch(*request, received, frame.size());
    }
    if (const auto* reply = std::get_if<QJsonObject>(&decoded)) {
        return *reply;
    }
    return std::nullopt;
}

std::optional<QJsonObject>
    Session::dispatch(const JsonRpc::Request& request, Clock::time_point received, qsizetype frameBytes)
{
    const QJsonValue& id = request.id;
    const QJsonObject params = request.params.toObject();
    const bool validParams = (request.params.isUndefined() || request.params.isObject()) &&
        (!params.contains("_meta") || params.value("_meta").isObject());
    if (id.isUndefined()) {
        if (validParams && request.method == "notifications/initialized" && phase == Phase::AwaitInitialized) {
            phase = Phase::Ready;
        }
        if (validParams && request.method == "notifications/cancelled") {
            cancel(params.value("requestId"));
        }
        return std::nullopt;
    }
    if (usedIds.contains(id)) {
        return JsonRpc::error(id, -32600, QStringLiteral("Request ID already used in this session"));
    }
    if (usedIds.size() >= MAX_SESSION_REQUESTS) {
        return JsonRpc::error(id, -32001, QStringLiteral("Session request limit reached. Start a new session."));
    }
    // A string value extracted from a JSON object can share the entire request container.
    usedIds.insert(id.isString() ? QJsonValue(id.toString()) : id);
    if (!validParams) {
        return JsonRpc::error(id, -32602, QStringLiteral("Expected an object for params and optional _meta"));
    }
    if (request.method == "ping") {
        return JsonRpc::success(id, {});
    }
    if (request.method == "initialize") {
        return initialize(id, params);
    }
    if (phase != Phase::Ready) {
        return JsonRpc::error(id, -32002, QStringLiteral("Complete initialization first"));
    }
    if (request.method == "tools/list") {
        return Tools::list(id, params);
    }
    if (request.method == "tools/call") {
        return Tools::call(id, params, [&](const QString& name, const QJsonObject& args) {
            return schedule(id, name, args, received, frameBytes);
        });
    }
    return JsonRpc::error(id, -32601, QStringLiteral("Method not found"));
}

QJsonObject Session::initialize(const QJsonValue& id, const QJsonObject& params)
{
    if (phase != Phase::AwaitInitialize) {
        return JsonRpc::error(id, -32600, QStringLiteral("Session already initialized"));
    }
    const QJsonObject clientInfo = params.value("clientInfo").toObject();
    if (!params.value("protocolVersion").isString() || !params.value("capabilities").isObject() ||
        !clientInfo.value("name").isString() || !clientInfo.value("version").isString())
    {
        return JsonRpc::error(
            id,
            -32602,
            QStringLiteral("Expected protocolVersion, capabilities and clientInfo with name and version"));
    }
    phase = Phase::AwaitInitialized;
    return JsonRpc::success(
        id,
        { { "protocolVersion", "2025-11-25" },
          { "capabilities", QJsonObject { { "tools", QJsonObject {} } } },
          { "serverInfo", QJsonObject { { "name", "treemcp" }, { "version", QString::fromLatin1(VERSION) } } } });
}

} // namespace treemcp
