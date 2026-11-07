// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reference images (core/Reference.h; View > Reference images): where each kind sits, that a scene plane fades out
// edge-on, the handles' hit test, the size caps, the commands, and that projects keep them (embedded or by path).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Reference.h"
#include "core/Micro.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>

using namespace sf;

namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }

// A w x h image file (a red/blue split, so flips show) at a temp path; returns the path.
std::string writeImage(const char* name, int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(size_t(w) * size_t(h) * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = img.rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
            p[0] = x < w / 2 ? 255 : 0;
            p[1] = 0;
            p[2] = x < w / 2 ? 0 : 255;
        }
    const std::string path = tempPath(name);
    std::string err;
    CHECK(img.savePng(path, err));
    return path;
}

AABB unitBox() {
    AABB b;
    b.expand(glm::vec3(-1.0f));
    b.expand(glm::vec3(1.0f));
    return b;
}

} // namespace

TEST(reference_behind_corners_and_flips) {
    ReferenceImage r;
    r.image = std::make_shared<const Image>(Image{200, 100, std::vector<uint8_t>(200 * 100 * 4, 255)});
    r.center = {0.5f, 0.5f};
    r.height = 0.5f;
    // A 2:1 image half the canvas tall on a 2:1 canvas: half its width too, centred.
    auto c = r.corners(2.0f);
    CHECK(near(c[0].x, 0.25f) && near(c[0].y, 0.25f));
    CHECK(near(c[2].x, 0.75f) && near(c[2].y, 0.75f));
    r.stretch = 2.0f; // twice as wide as its proportions
    c = r.corners(2.0f);
    CHECK(near(c[0].x, 0.0f) && near(c[1].x, 1.0f));
    CHECK(r.cornerUv(0) == glm::vec2(0, 0) && r.cornerUv(2) == glm::vec2(1, 1));
    r.flipX = true;
    CHECK(r.cornerUv(0) == glm::vec2(1, 0));
    r.flipY = true;
    CHECK(r.cornerUv(0) == glm::vec2(1, 1));
}

TEST(reference_scene_planes_sit_behind_the_model) {
    ReferenceImage r;
    r.image = std::make_shared<const Image>(Image{100, 100, std::vector<uint8_t>(100 * 100 * 4, 255)});
    r.mode = ReferenceMode::Scene;
    for (ReferencePlane p : {ReferencePlane::Front, ReferencePlane::Side, ReferencePlane::Top}) {
        r.plane = p;
        r.placeInScene(unitBox());
        glm::vec3 u, v, n;
        r.axes(u, v, n);
        // Behind the box as its view sees it (the view looks along -n), as tall as the box plus a margin.
        CHECK(glm::dot(r.origin, n) < -1.0f);
        CHECK(r.sceneHeight > 2.0f && r.sceneHeight < 2.5f);
        const auto c = r.corners(1.0f);
        for (const glm::vec3& k : c) CHECK(near(glm::dot(k, n), glm::dot(r.origin, n))); // (flat, in its plane)
        CHECK(near(glm::dot(u, n), 0.0f) && near(glm::dot(v, n), 0.0f) && near(glm::dot(u, v), 0.0f)); // (orthonormal)
    }
    // Front plane: seen from the front it shows whole; from the side (edge-on) it is gone.
    r.plane = ReferencePlane::Front;
    r.placeInScene(unitBox());
    const glm::mat4 front = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 side = glm::lookAt(glm::vec3(5, 0, r.origin.z), glm::vec3(0, 0, r.origin.z), glm::vec3(0, 1, 0));
    CHECK(near(r.facing(front, true), 1.0f));
    CHECK(near(r.facing(front, false), 1.0f, 1e-3f));
    CHECK(r.facing(side, true) == 0.0f);
    CHECK(r.facing(side, false) == 0.0f);
    r.mode = ReferenceMode::Behind; // a backdrop never fades
    CHECK(r.facing(side, true) == 1.0f);
}

// Outside Adjust nothing asks this, so references take no picks; inside Adjust only the corners and the inside do.
TEST(reference_handle_hit_test) {
    const std::array<glm::vec2, 4> c = {glm::vec2(100, 100), glm::vec2(300, 100), glm::vec2(300, 200), glm::vec2(100, 200)};
    CHECK(referenceHandleAt(c, {102, 98}, 8.0f) == 1);
    CHECK(referenceHandleAt(c, {300, 205}, 8.0f) == 3);
    CHECK(referenceHandleAt(c, {200, 150}, 8.0f) == 0);
    CHECK(referenceHandleAt(c, {50, 150}, 8.0f) == -1);
    CHECK(referenceHandleAt(c, {200, 250}, 8.0f) == -1);
}

// A scene plane right in front of the model changes nothing for the brushes: they ray-cast the mesh only.
TEST(reference_never_takes_picks) {
    Document doc = makeDoc(makeIcosphere(3));
    CommandRegistry reg;
    const std::string path = writeImage("sf_ref_pick.png", 8, 8);
    RayHit before, after;
    CHECK(surfaceRaycast(doc.mesh, doc.bvh, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), before));
    CHECK(reg.execute(doc, "reference_add", {{"path", path}, {"mode", std::string("scene")}, {"origin_z", 2.0}, {"opacity", 1.0}}).ok);
    CHECK(surfaceRaycast(doc.mesh, doc.bvh, glm::vec3(0, 0, 5), glm::vec3(0, 0, -1), after));
    CHECK(near(before.position.z, after.position.z));
    std::filesystem::remove(path);
}

TEST(reference_commands_and_caps) {
    Document doc = makeDoc(makeIcosphere(2));
    CommandRegistry reg;
    const std::string small = writeImage("sf_ref_small.png", 64, 32);
    CHECK(reg.execute(doc, "reference_add", {{"path", small}}).ok);
    CHECK(doc.references.size() == 1);
    const ReferenceImage& r = doc.references[0];
    CHECK(r.mode == ReferenceMode::Behind && r.image && r.image->width == 64);
    CHECK(r.fileBytes && !r.fileBytes->empty()); // (embeds its own file, unchanged)
    CHECK(doc.dirty());
    CHECK(reg.execute(doc, "reference_set", {{"mode", std::string("scene")}, {"plane", std::string("side")}}).ok);
    CHECK(doc.references[0].mode == ReferenceMode::Scene && doc.references[0].plane == ReferencePlane::Side);
    CHECK(doc.references[0].sceneHeight > 0.0f && doc.references[0].origin.x < -1.0f);
    CHECK(!reg.execute(doc, "reference_set", {{"mode", std::string("sideways")}}).ok);
    CHECK(!reg.execute(doc, "reference_set", {{"index", 5.0}, {"opacity", 1.0}}).ok);
    CHECK(reg.execute(doc, "reference_set", {{"opacity", 7.0}}).ok && doc.references[0].opacity == 1.0f); // (clamped)
    CHECK(!reg.execute(doc, "reference_add", {{"path", tempPath("sf_ref_missing.png")}}).ok);

    // Bigger than the cap: scaled down, and then saved as a PNG of what it keeps (not the original file).
    const std::string big = writeImage("sf_ref_big.png", kReferenceMaxSide + 200, 8);
    CHECK(reg.execute(doc, "reference_add", {{"path", big}}).ok);
    CHECK(doc.references[1].image->width == kReferenceMaxSide && !doc.references[1].fileBytes);

    CHECK(reg.execute(doc, "reference_pair", {{"front", small}, {"side", small}}).ok);
    CHECK(doc.references.size() == 4);
    CHECK(doc.references[2].plane == ReferencePlane::Front && doc.references[3].plane == ReferencePlane::Side);
    CHECK(near(doc.references[2].sceneHeight, doc.references[3].sceneHeight));
    CHECK(referenceGpuBytes(doc.references) > 0);
    while (doc.references.size() < size_t(kMaxReferences)) CHECK(reg.execute(doc, "reference_add", {{"path", small}}).ok);
    CHECK(!reg.execute(doc, "reference_add", {{"path", small}}).ok); // (at most kMaxReferences)
    CHECK(reg.execute(doc, "reference_remove", {{"index", 0.0}}).ok && doc.references.size() == size_t(kMaxReferences) - 1);
    CHECK(reg.execute(doc, "reference_remove", {{"all", true}}).ok && doc.references.empty());
    std::filesystem::remove(small);
    std::filesystem::remove(big);
}

TEST(reference_project_round_trip) {
    Document doc = makeDoc(makeIcosphere(2));
    CommandRegistry reg;
    const std::string img = writeImage("sf_ref_saved.png", 40, 20);
    const std::string linked = writeImage("sf_ref_linked.png", 30, 30);
    CHECK(reg.execute(doc, "reference_add", {{"path", img}, {"x", 0.25}, {"height", 0.4}, {"flip_x", true}, {"opacity", 0.7}}).ok);
    CHECK(reg.execute(doc, "reference_add", {{"path", linked}, {"mode", std::string("scene")}, {"plane", std::string("top")},
                                              {"embed", false}, {"locked", true}, {"in_renders", true}, {"stretch", 1.5}})
              .ok);
    const ReferenceImage scene = doc.references[1];
    const std::string path = tempPath("sf_ref_round.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    std::filesystem::remove(img); // the embedded one no longer needs its file

    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.references.size() == 2);
    const ReferenceImage& a = back.references[0];
    CHECK(a.image && a.image->width == 40 && a.image->height == 20);
    CHECK(a.mode == ReferenceMode::Behind && near(a.center.x, 0.25f) && near(a.height, 0.4f) && a.flipX && near(a.opacity, 0.7f));
    CHECK(a.embed && a.visible && !a.locked && !a.inRenders);
    CHECK(a.image->rgba[0] == 255 && a.image->rgba[2] == 0); // (the left half red, as saved)
    const ReferenceImage& b = back.references[1];
    CHECK(b.image && b.image->width == 30 && !b.embed && b.locked && b.inRenders);
    CHECK(b.mode == ReferenceMode::Scene && b.plane == ReferencePlane::Top && near(b.stretch, 1.5f));
    CHECK(near(b.origin.y, scene.origin.y) && near(b.sceneHeight, scene.sceneHeight));

    // By path only, and the file moved: the reference stays (the panel says it is missing), without its image.
    std::filesystem::remove(linked);
    Document missing;
    CHECK(loadProject(path, missing, err));
    CHECK(missing.references.size() == 2 && missing.references[0].image && !missing.references[1].image);
    CHECK(!missing.references[1].shown());
    std::filesystem::remove(path);
}
