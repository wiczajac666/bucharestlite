#pragma once

#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_core/time.hpp>
#include <bl_render/effect.hpp>
#include <bl_render/timeline_clip_thumbnail_engine.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>

#include <memory>
#include <optional>
#include <string>
#include <vector>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
QT_END_NAMESPACE

namespace bl::ui {

// Stored on every scene item that wants to talk back to the model.
constexpr int kClipItemIdRole = Qt::UserRole;
constexpr int kClipItemTrackRole = Qt::UserRole + 1;

// Shared by the ruler ticks and the timeline's position indicator so both
// switch units together. `showFrames` renders a plain frame count, otherwise
// HH:MM:SS:FF at the sequence frame rate.
QString formatTimelinePosition(int64_t frame, const bl::Rational& fps,
                               bool showFrames);

// ---------------------------------------------------------------------------
// Ruler: frame ticks + labels along the top of the timeline.
// ---------------------------------------------------------------------------
class RulerItem : public QGraphicsRectItem {
public:
    explicit RulerItem(qreal height);

    void configure(const bl::SequenceSettings& settings);
    void setWidth(qreal width);
    void setZoom(qreal pxPerFrame);
    // Phase 3 item 5: switch ruler labels between timecode and plain frames.
    void setShowFrames(bool on) { showFrames_ = on; }
    bool showFrames() const { return showFrames_; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bl::SequenceSettings settings_;
    qreal pxPerFrame_{6.0};
    bool showFrames_{false};
};

// ---------------------------------------------------------------------------
// Playhead: the vertical current-time line spanning ruler + lanes.
// ---------------------------------------------------------------------------
class PlayheadItem : public QGraphicsLineItem {
public:
    PlayheadItem();

    void setHeight(qreal height);
};

// ---------------------------------------------------------------------------
// Snap indicator: transient dashed vertical guide shown while snapping.
// ---------------------------------------------------------------------------
class SnapIndicatorItem : public QGraphicsLineItem {
public:
    SnapIndicatorItem();

    void setHeight(qreal height);
    void setActive(bool active) { setVisible(active); }
};

// ---------------------------------------------------------------------------
// Zone: the timeline in/out range band (spans ruler + lanes). Drawn behind the
// clips so it reads as a selection band; the in/out edges get grab handles.
// ---------------------------------------------------------------------------
class ZoneItem : public QGraphicsRectItem {
public:
    ZoneItem();

    void setRange(qreal x, qreal width, qreal top, qreal height);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;
};

// ---------------------------------------------------------------------------
// Marker: a flagged frame (triangle in the ruler + thin guide + text chip).
// ---------------------------------------------------------------------------
class MarkerItem : public QGraphicsRectItem {
public:
    MarkerItem(const bl::Marker& marker, qreal x, qreal topY, qreal height);

    const bl::Marker& marker() const { return marker_; }
    // Phase 3 item 4: whether the marker's comment is drawn under its label.
    void setShowComment(bool on) { showComment_ = on; }
    bool showComment() const { return showComment_; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bl::Marker marker_;
    bool showComment_{true};
};

// ---------------------------------------------------------------------------
// Lane: translucent background band for a single track.
// ---------------------------------------------------------------------------
class LaneItem : public QGraphicsRectItem {
public:
    LaneItem(qreal y, qreal height, qreal width, bool videoKind);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    bool videoKind_;
};

// ---------------------------------------------------------------------------
// Clip: a rect with name label, color-tag bar, effect chips, trim-handle hit
// zones, transition hatch and subtitle/proxy badges. Scene position follows
// the model; during a drag the view sets a preview rectangle via
// setPreviewRect() without touching the model.
// ---------------------------------------------------------------------------
class ClipItem : public QGraphicsRectItem {
public:
    enum class Edge { None, Left, Right };

    ClipItem(const bl::Clip& clip, int flatTrack, qreal pxPerFrame,
             const bl::Rational& fps);

    const bl::Clip& clip() const { return clip_; }
    int flatTrack() const { return flatTrack_; }
    qreal pxPerFrame() const { return pxPerFrame_; }

    QRectF geometry() const;
    Edge edgeAt(const QPointF& scenePos) const;
    qreal handleWidth() const { return 6.0; }

    void setZoom(qreal pxPerFrame);
    void setLaneRect(const QRectF& laneRect);
    void setPreviewRect(const QRectF& rect);
    void clearPreview();
    bool hasPreview() const { return preview_.has_value(); }

    // Audio waveform (P1 #13). The analyzed envelope for this clip's source is
    // resolved by the panel and handed in; a null/empty envelope (video-only
    // clip, or audio not analyzed yet) draws nothing. Painting reduces the
    // envelope to one bar per pixel column over the clip's source range.
    void setWaveform(std::shared_ptr<const bl::AudioMeterEnvelope> envelope) {
        waveform_ = std::move(envelope);
    }
    const std::shared_ptr<const bl::AudioMeterEnvelope>& waveform() const {
        return waveform_;
    }

    // Video filmstrip (Phase 3). The panel hands in one sampled thumbnail per
    // cell across the clip's source window; cells still decoding are null and
    // drawn as a placeholder until onReady repaints. A null strip (audio-only
    // clip, or no resolver) draws nothing.
    void setThumbnail(std::shared_ptr<const bl::ClipThumbnailStrip> strip) {
        thumbnail_ = std::move(strip);
    }
    const std::shared_ptr<const bl::ClipThumbnailStrip>& thumbnail() const {
        return thumbnail_;
    }

    // Effect indicators (Phase 3 item 2). The panel resolves the clip's effect
    // ids to display names and hands them in; the paint code draws truncated
    // chips at the top-left, dimming bypassed effects. No list draws nothing.
    void setEffectDisplays(std::vector<bl::EffectDisplay> displays) {
        effectDisplays_ = std::move(displays);
    }
    const std::vector<bl::EffectDisplay>& effectDisplays() const {
        return effectDisplays_;
    }

    // Whether supplementary clip labels (the color-tag bar and effect chips)
    // are drawn. The clip name itself is always visible.
    void setShowLabels(bool on) { showLabels_ = on; }
    bool showLabels() const { return showLabels_; }

    // Proxy override: true = use proxy, false = use original
    void setUseProxy(bool useProxy) {
        useProxy_ = useProxy;
    }
    bool useProxy() const { return useProxy_; }

    // Proxy status badge: "none", "generating", "ready", "failed"
    void setProxyStatus(const QString& status) {
        proxyStatus_ = status;
    }
    const QString& proxyStatus() const { return proxyStatus_; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    // Clip-local markers (Phase 3 item 3) are painted from clip_.markers, the
    // single source of truth; a model edit rebuilds the item. Triangles sit on
    // the bottom edge so they stay clear of effect chips and right-hand badges.
    void paintClipMarkers(QPainter* painter, const QRectF& r, bool selected);

    bl::Clip clip_;
    int flatTrack_{0};
    qreal pxPerFrame_{6.0};
    bl::Rational fps_;
    QRectF laneRect_;
    std::optional<QRectF> preview_;
    std::shared_ptr<const bl::AudioMeterEnvelope> waveform_;
    std::shared_ptr<const bl::ClipThumbnailStrip> thumbnail_;
    std::vector<bl::EffectDisplay> effectDisplays_;
    bool showLabels_{true};
    bool useProxy_{true};  // Default to using proxy when available
    QString proxyStatus_{QStringLiteral("none")};
};

namespace timeline_geometry {

constexpr qreal kRulerHeight = 26.0;
constexpr qreal kDefaultVideoLaneHeight = 44.0;
constexpr qreal kDefaultAudioLaneHeight = 36.0;
constexpr qreal kMinLaneHeight = 18.0;
constexpr qreal kMaxLaneHeight = 200.0;
// Right-hand margin (px) added to contentWidth so the last clip never sits
// flush against the viewport edge; also the minimum timeline footprint. Public
// so the panel can solve for a fit-to-viewport zoom (contentWidth is linear in
// pxPerFrame: width = endFrame * pxPerFrame + kContentMargin).
constexpr qreal kContentMargin = 56.0;

// Kdenlive's three track-layout modes, expressed over flat indices (video
// tracks first, then audio). Split — the app default — is today's layout
// (video lanes on top, audio below); Mixed interleaves each audio lane under
// its video lane; MixedReverse puts the audio lane above. P4 exposes the
// choice in the UI; until then every call site uses the default.
enum class LaneOrder { Split, Mixed, MixedReverse };

// Flat indices ordered top to bottom for the given layout.
std::vector<int> lanePermutation(int videoCount, int audioCount,
                                 LaneOrder order);
// Lane position of a flat track (-1 when out of range), and its inverse.
int laneForFlat(const bl::Sequence& seq, int flat,
                LaneOrder order = LaneOrder::Split);
int flatForLane(const bl::Sequence& seq, int lane,
                LaneOrder order = LaneOrder::Split);

// Returns the height of a lane based on the track's stored height, falling
// back to kind-specific defaults, then scaled by the view-only vertical zoom.
// The model height is clamped to [kMinLaneHeight, kMaxLaneHeight] first, so a
// zoom of 1 keeps the previous layout exactly.
qreal laneHeight(const bl::Sequence& seq, int flat, int videoCount,
                 qreal verticalScale = 1.0);

// Returns the top Y position of a lane in scene coordinates (including ruler).
qreal laneTop(const bl::Sequence& seq, int flat, int videoCount,
              LaneOrder order = LaneOrder::Split, qreal verticalScale = 1.0);

qreal contentWidth(const bl::Sequence& seq, qreal pxPerFrame);
qreal contentHeight(const bl::Sequence& seq, qreal verticalScale = 1.0);

} // namespace timeline_geometry

} // namespace bl::ui