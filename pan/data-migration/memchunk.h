/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __MemChunk_h__
#define __MemChunk_h__

#include <cstddef>

namespace pan {

template <class T> class MemChunk
{
  public:
    void push_back(T const &src)
    {
      if (chunks_ == nullptr || chunks_->count_ == chunks_->elements_)
      {
        grow(nelem);
      }
      T *thead = head_;
      new (thead) T(src);
      back_ = thead;
      head_ += 1;
      chunks_->count_ += 1;
    }

    T &back()
    {
      return *back_;
    }

    T *alloc()
    {
      push_back(T());
      return back_;
    }

    MemChunk() :
      chunks_(nullptr),
      back_(nullptr),
      head_(nullptr),
      allocated_(0)
    {
    }

    ~MemChunk()
    {
      while (chunks_ != nullptr)
      {
        T *t = chunks_->data();
        for (int i = 0; i < chunks_->count_; i += 1)
        {
          t[i].~T();
        }
        Chunk *p = chunks_;
        chunks_ = chunks_->next_;
        ::operator delete(p);
      }
    }

    template <class U> MemChunk(MemChunk<U> &) = delete;
    MemChunk *operator=(MemChunk const &) = delete;

    /** Ensures there's enough space for the specified number of elements */
    void reserve(std::size_t elements)
    {
      if (elements > allocated_)
      {
        grow(elements - allocated_);
      }
    }

  private:
    struct Chunk
    {
        Chunk *next_;
        std::size_t elements_;
        std::size_t count_;

        T *data()
        {
          return reinterpret_cast<T *>(this + 1);
        }
    };

    void grow(std::size_t elements)
    {
      Chunk *c = static_cast<Chunk *>(
        ::operator new(sizeof(Chunk) + elements * sizeof(T))
      );

      c->next_ = chunks_;
      c->elements_ = elements;
      c->count_ = 0;
      chunks_ = c;
      head_ = c->data();
      allocated_ += elements;
    };

    enum { nelem = 16 * 1024 };

    Chunk *chunks_;
    T *back_;
    T *head_;
    std::size_t allocated_;
};

} // namespace pan
#endif
