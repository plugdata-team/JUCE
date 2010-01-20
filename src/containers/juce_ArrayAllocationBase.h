/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-9 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

#ifndef __JUCE_ARRAYALLOCATIONBASE_JUCEHEADER__
#define __JUCE_ARRAYALLOCATIONBASE_JUCEHEADER__

#include "juce_HeapBlock.h"


//==============================================================================
/**
    Implements some basic array storage allocation functions.

    This class isn't really for public use - it's used by the other
    array classes, but might come in handy for some purposes.

    @see Array, OwnedArray, ReferenceCountedArray
*/
template <class ElementType>
class ArrayAllocationBase
{
public:
    //==============================================================================
    /** Creates an empty array. */
    ArrayAllocationBase() throw()
        : numAllocated (0)
    {
    }

    /** Destructor. */
    ~ArrayAllocationBase()
    {
    }

    //==============================================================================
    /** Changes the amount of storage allocated.

        This will retain any data currently held in the array, and either add or
        remove extra space at the end.

        @param numElements  the number of elements that are needed
    */
    void setAllocatedSize (const int numElements)
    {
        if (numAllocated != numElements)
        {
            if (numElements > 0)
                elements.realloc (numElements);
            else
                elements.free();

            numAllocated = numElements;
        }
    }

    /** Increases the amount of storage allocated if it is less than a given amount.

        This will retain any data currently held in the array, but will add
        extra space at the end to make sure there it's at least as big as the size
        passed in. If it's already bigger, no action is taken.

        @param minNumElements  the minimum number of elements that are needed
    */
    void ensureAllocatedSize (int minNumElements)
    {
        if (minNumElements > numAllocated)
            setAllocatedSize ((minNumElements + minNumElements / 2 + 8) & ~7);
    }

    /** Minimises the amount of storage allocated so that it's no more than
        the given number of elements.
    */
    void shrinkToNoMoreThan (int maxNumElements)
    {
        if (maxNumElements < numAllocated)
            setAllocatedSize (maxNumElements);
    }
    
    /** Swap the contents of two objects. */
    void swapWith (ArrayAllocationBase <ElementType>& other)
    {
        elements.swapWith (other.elements);
        swapVariables (numAllocated, other.numAllocated);
    }

    //==============================================================================
    HeapBlock <ElementType> elements;
    int numAllocated;

private:
    ArrayAllocationBase (const ArrayAllocationBase&);
    const ArrayAllocationBase& operator= (const ArrayAllocationBase&);
};


#endif   // __JUCE_ARRAYALLOCATIONBASE_JUCEHEADER__
