#pragma once

namespace jfd::media {

// What a video item's scene graph sync achieved.
enum class RenderSetup
{
  // The render context and texture exist: show the node.
  Ready,
  // The render context could not be created yet: moving the video to another
  // window frees the old window's context on its own render thread, possibly
  // a moment after the new one asks (mpv allows one per core). Ask again on
  // the next frame; nothing is reported.
  Retry,
  // Reported through renderError(); stop trying.
  Failed,
};

// Counts render context attempts of one video item across its nodes, so a
// node dropped for a retry does not reset the count.
class RenderContextAttempts
{
public:
  static constexpr int Maximum = 100;

  // After mpv_render_context_create failed: Retry while attempts remain.
  RenderSetup failed() { return ++m_attempts < Maximum ? RenderSetup::Retry : RenderSetup::Failed; }
  void succeeded() { m_attempts = 0; }
  int count() const { return m_attempts; }

private:
  int m_attempts = 0;
};

}
