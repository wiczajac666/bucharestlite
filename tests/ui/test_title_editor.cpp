#include <bl_timeline/title.hpp>

#include <app/project_controller.hpp>
#include <dialogs/title_editor_dialog.hpp>
#include <title/title_renderer.hpp>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPlainTextEdit>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <utility>

namespace {

using bl::TitleAlign;
using bl::TitleColor;
using bl::TitleSpec;
using bl::ui::renderTitleImage;
using bl::ui::saveTitlePng;

int opaquePixels(const QImage& img) {
    int count = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 0) ++count;
        }
    }
    return count;
}

bool hasPixelNear(const QImage& img, int r, int g, int b, int tol = 40) {
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = img.pixel(x, y);
            if (qAlpha(p) < 200) continue;
            if (std::abs(qRed(p) - r) <= tol && std::abs(qGreen(p) - g) <= tol &&
                std::abs(qBlue(p) - b) <= tol) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST(TitleRenderer, EmptyTextProducesTransparentCanvas) {
    TitleSpec spec;
    spec.text.clear();
    const QImage img = renderTitleImage(spec, 320, 180);
    ASSERT_FALSE(img.isNull());
    EXPECT_EQ(img.width(), 320);
    EXPECT_EQ(img.height(), 180);
    EXPECT_EQ(opaquePixels(img), 0);
}

TEST(TitleRenderer, NonPositiveSizeGivesNullImage) {
    EXPECT_TRUE(renderTitleImage(TitleSpec{}, 0, 100).isNull());
    EXPECT_TRUE(renderTitleImage(TitleSpec{}, 100, -1).isNull());
}

TEST(TitleRenderer, DrawsTextPixels) {
    TitleSpec spec;
    spec.text = "A";
    spec.fontSizePx = 120.0;
    const QImage img = renderTitleImage(spec, 320, 180);
    EXPECT_GT(opaquePixels(img), 0);
}

TEST(TitleRenderer, TextHonoursColour) {
    TitleSpec spec;
    spec.text = "M";
    spec.fontSizePx = 200.0;
    spec.textColor = TitleColor{255, 0, 0, 255};
    const QImage img = renderTitleImage(spec, 400, 400);
    EXPECT_TRUE(hasPixelNear(img, 255, 0, 0));
}

TEST(TitleRenderer, BackgroundFillsBehindText) {
    TitleSpec spec;
    spec.text = "Bg";
    spec.fontSizePx = 60.0;
    spec.backgroundEnabled = true;
    spec.backgroundColor = TitleColor{10, 200, 30, 255};
    spec.paddingPx = 40.0;
    const QImage img = renderTitleImage(spec, 400, 200);

    // The background colour appears somewhere, fully opaque.
    EXPECT_TRUE(hasPixelNear(img, 10, 200, 30, 10));

    // Enabling the background adds opaque coverage around the glyphs.
    TitleSpec noBackground = spec;
    noBackground.backgroundEnabled = false;
    const QImage plain = renderTitleImage(noBackground, 400, 200);
    EXPECT_GT(opaquePixels(img), opaquePixels(plain));
}

TEST(TitleRenderer, AnchorMovesTextBlock) {
    TitleSpec topLeft;
    topLeft.text = "A";
    topLeft.fontSizePx = 80.0;
    topLeft.align = TitleAlign::Left;
    topLeft.anchorX = 0.0;
    topLeft.anchorY = 0.0;

    TitleSpec bottomRight = topLeft;
    bottomRight.align = TitleAlign::Right;
    bottomRight.anchorX = 1.0;
    bottomRight.anchorY = 1.0;

    const QImage a = renderTitleImage(topLeft, 400, 200);
    const QImage b = renderTitleImage(bottomRight, 400, 200);

    auto centreOfMass = [](const QImage& img) {
        double sx = 0.0, sy = 0.0, n = 0.0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (qAlpha(img.pixel(x, y)) > 0) {
                    sx += x;
                    sy += y;
                    n += 1.0;
                }
            }
        }
        return n > 0.0 ? std::pair<double, double>{sx / n, sy / n}
                       : std::pair<double, double>{-1.0, -1.0};
    };
    const auto ca = centreOfMass(a);
    const auto cb = centreOfMass(b);
    ASSERT_GE(ca.first, 0.0);
    ASSERT_GE(cb.first, 0.0);
    EXPECT_LT(ca.first, cb.first);
    EXPECT_LT(ca.second, cb.second);
}

TEST(TitleRenderer, SavePngWritesReadableFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("title.png"));

    TitleSpec spec;
    spec.text = "Saved";
    ASSERT_TRUE(saveTitlePng(spec, 160, 90, path));
    ASSERT_TRUE(QFile::exists(path));

    const QImage reloaded(path);
    ASSERT_FALSE(reloaded.isNull());
    EXPECT_EQ(reloaded.width(), 160);
    EXPECT_EQ(reloaded.height(), 90);
    EXPECT_GT(opaquePixels(reloaded.convertToFormat(QImage::Format_ARGB32)), 0);
}

TEST(TitleEditorDialog, DefaultsMatchDefaultSpec) {
    bl::ui::TitleEditorDialog dialog;
    const TitleSpec spec = dialog.spec();
    EXPECT_EQ(spec.text, "Title");
    EXPECT_EQ(spec.align, TitleAlign::Center);
    EXPECT_NEAR(spec.anchorX, 0.5, 1e-9);
}

TEST(TitleEditorDialog, SetSpecRoundTrips) {
    bl::ui::TitleEditorDialog dialog;
    TitleSpec spec;
    spec.text = "Hello\nWorld";
    spec.bold = true;
    spec.italic = true;
    spec.align = TitleAlign::Right;
    spec.anchorX = 0.25;
    spec.anchorY = 0.75;
    spec.backgroundEnabled = true;
    spec.paddingPx = 30.0;
    spec.outlinePx = 4.0;
    spec.textColor = TitleColor{1, 2, 3, 4};

    dialog.setSpec(spec);
    const TitleSpec back = dialog.spec();

    EXPECT_EQ(back.text, spec.text);
    EXPECT_EQ(back.bold, spec.bold);
    EXPECT_EQ(back.italic, spec.italic);
    EXPECT_EQ(back.align, spec.align);
    EXPECT_NEAR(back.anchorX, spec.anchorX, 1e-9);
    EXPECT_NEAR(back.anchorY, spec.anchorY, 1e-9);
    EXPECT_EQ(back.backgroundEnabled, spec.backgroundEnabled);
    EXPECT_NEAR(back.paddingPx, spec.paddingPx, 1e-9);
    EXPECT_NEAR(back.outlinePx, spec.outlinePx, 1e-9);
    EXPECT_EQ(back.textColor, spec.textColor);
}

TEST(TitleEditorDialog, WidgetEditsFlowIntoSpec) {
    bl::ui::TitleEditorDialog dialog;
    auto* text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("titleText"));
    auto* align = dialog.findChild<QComboBox*>(QStringLiteral("titleAlign"));
    auto* size = dialog.findChild<QSpinBox*>(QStringLiteral("titleFontSize"));
    auto* anchorX =
        dialog.findChild<QSlider*>(QStringLiteral("titleAnchorX"));
    ASSERT_NE(text, nullptr);
    ASSERT_NE(align, nullptr);
    ASSERT_NE(size, nullptr);
    ASSERT_NE(anchorX, nullptr);

    text->setPlainText(QStringLiteral("Changed"));
    align->setCurrentIndex(2);  // Right
    size->setValue(120);
    anchorX->setValue(20);

    const TitleSpec spec = dialog.spec();
    EXPECT_EQ(spec.text, "Changed");
    EXPECT_EQ(spec.align, TitleAlign::Right);
    EXPECT_DOUBLE_EQ(spec.fontSizePx, 120.0);
    EXPECT_NEAR(spec.anchorX, 0.2, 1e-9);
}

TEST(TitleEditorDialog, DurationAndCanvasSize) {
    bl::ui::TitleEditorDialog dialog;
    dialog.setDurationSeconds(7.0);
    EXPECT_DOUBLE_EQ(dialog.durationSeconds(), 7.0);
    dialog.setCanvasSize(640, 360);
    EXPECT_EQ(dialog.canvasWidth(), 640);
    EXPECT_EQ(dialog.canvasHeight(), 360);
    const QImage img = dialog.renderAtCanvas();
    ASSERT_FALSE(img.isNull());
    EXPECT_EQ(img.width(), 640);
    EXPECT_EQ(img.height(), 360);
}

TEST(ProjectControllerTitle, AddTitleStoresSpecAndIsUndoable) {
    bl::ui::ProjectController controller;
    controller.newProject(QStringLiteral("Titles"));

    TitleSpec spec;
    spec.text = "Hello";
    const nlohmann::json json = spec;

    ASSERT_TRUE(controller.addTitleToMediaBin(QStringLiteral("/media/t.png"),
                                              QStringLiteral("Hello"), json));
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    const std::string id = controller.mediaBin()[0].id;
    EXPECT_EQ(controller.mediaBin()[0].name, "Hello");
    EXPECT_EQ(controller.mediaIdForPath(QStringLiteral("/media/t.png")), id);
    ASSERT_FALSE(controller.titleSpecFor(id).is_null());
    EXPECT_EQ(controller.titleSpecFor(id).get<TitleSpec>().text, "Hello");

    // Undo removes media item and spec together; redo restores both.
    controller.undoStack().undo();
    EXPECT_TRUE(controller.mediaBin().empty());
    EXPECT_TRUE(controller.titleSpecFor(id).is_null());
    controller.undoStack().redo();
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    EXPECT_FALSE(controller.titleSpecFor(id).is_null());

    // Removing a title also drops its spec; undo restores it.
    ASSERT_TRUE(controller.removeFromMediaBin(QString::fromStdString(id)));
    EXPECT_TRUE(controller.titleSpecFor(id).is_null());
    controller.undoStack().undo();
    EXPECT_FALSE(controller.titleSpecFor(id).is_null());
}

TEST(ProjectControllerTitle, UnknownIdsAndPathsReturnEmpty) {
    bl::ui::ProjectController controller;
    controller.newProject(QStringLiteral("Titles"));
    EXPECT_TRUE(controller.titleSpecFor("nope").is_null());
    EXPECT_TRUE(controller.mediaIdForPath(QStringLiteral("/nope.png")).empty());
}

TEST(TitleEditorDialog, DurationRowVisibilityIsToggleable) {
    bl::ui::TitleEditorDialog dialog;
    auto* duration =
        dialog.findChild<QSpinBox*>(QStringLiteral("titleDuration"));
    ASSERT_NE(duration, nullptr);
    dialog.setDurationVisible(false);
    EXPECT_FALSE(duration->isVisibleTo(&dialog));
    dialog.setDurationVisible(true);
    EXPECT_TRUE(duration->isVisibleTo(&dialog));
}

TEST(ProjectControllerTitle, UpdateTitleRewritesSpecNameAndPngUndoably) {
    bl::ui::ProjectController controller;
    controller.newProject(QStringLiteral("Titles"));

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString png = QDir(dir.path()).filePath(QStringLiteral("t.png"));

    TitleSpec original;
    original.text = "Original";
    ASSERT_TRUE(controller.addTitleToMediaBin(
        png, QStringLiteral("Original"), nlohmann::json(original)));
    ASSERT_EQ(controller.mediaBin().size(), 1u);
    const std::string id = controller.mediaBin()[0].id;

    TitleSpec edited;
    edited.text = "Edited";
    edited.textColor = TitleColor{10, 20, 30, 255};
    ASSERT_TRUE(controller.updateTitle(id, edited, 320, 180,
                                       QStringLiteral("Edited")));

    EXPECT_EQ(controller.mediaBin()[0].name, "Edited");
    EXPECT_EQ(controller.titleSpecFor(id).get<TitleSpec>().text, "Edited");
    EXPECT_EQ(controller.titleSpecFor(id).get<TitleSpec>().textColor,
              (TitleColor{10, 20, 30, 255}));

    QImage written;
    ASSERT_TRUE(written.load(png));
    EXPECT_EQ(written.width(), 320);
    EXPECT_EQ(written.height(), 180);

    // Undo restores the previous design, name and still; redo re-applies.
    controller.undoStack().undo();
    EXPECT_EQ(controller.mediaBin()[0].name, "Original");
    EXPECT_EQ(controller.titleSpecFor(id).get<TitleSpec>().text, "Original");
    EXPECT_TRUE(QFile::exists(png));

    controller.undoStack().redo();
    EXPECT_EQ(controller.mediaBin()[0].name, "Edited");
    EXPECT_EQ(controller.titleSpecFor(id).get<TitleSpec>().text, "Edited");
}

TEST(ProjectControllerTitle, UpdateTitleRefusesNonTitlesAndWriteFailures) {
    bl::ui::ProjectController controller;
    controller.newProject(QStringLiteral("Titles"));

    // Unknown id → false.
    EXPECT_FALSE(controller.updateTitle("nope", TitleSpec{}, 320, 180));

    // A plain (non-title) media item → false.
    ASSERT_TRUE(controller.addToMediaBin(QStringLiteral("/media/clip.mp4")));
    const std::string plainId = controller.mediaBin().back().id;
    EXPECT_FALSE(controller.updateTitle(plainId, TitleSpec{}, 320, 180));

    // A title whose directory cannot be written: the rewrite fails before any
    // command is pushed, so undo history is untouched.
    ASSERT_TRUE(controller.addTitleToMediaBin(
        QStringLiteral("/no-such-dir-bltest/t.png"), QStringLiteral("T"),
        nlohmann::json(TitleSpec{})));
    const std::string titleId = controller.mediaBin().back().id;
    const size_t depth = controller.undoStack().count();
    EXPECT_FALSE(controller.updateTitle(titleId, TitleSpec{}, 64, 64));
    EXPECT_EQ(controller.undoStack().count(), depth);
    EXPECT_EQ(controller.titleSpecFor(titleId).get<TitleSpec>().text,
              TitleSpec{}.text);
}
