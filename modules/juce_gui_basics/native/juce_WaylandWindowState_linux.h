/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-9-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#pragma once

namespace juce
{

/** Returns true if the peer is a Wayland window rather than an X11 one. */
JUCE_API bool isWaylandComponentPeer (const ComponentPeer*);

/** Asks the compositor to maximise or restore a top-level Wayland window.

    The request is asynchronous: the window is resized once the compositor configures it.
    Returns false if the peer is not a top-level Wayland window.
*/
JUCE_API bool setWaylandWindowMaximised (ComponentPeer&, bool shouldBeMaximised);

/** Returns true if the compositor has configured a top-level Wayland window as maximised.

    This changes in the same configure as the window's size, so it only reflects a
    setWaylandWindowMaximised() request once the compositor has answered it.
*/
JUCE_API bool isWaylandWindowMaximised (const ComponentPeer&);

} // namespace juce
