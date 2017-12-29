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

#include <cstring>

namespace pan {
  template <class T> class MemChunk
  {
    public:
      void push_back(const T& src)
      {
        if (count==nelem) grow();
        T* thead=head;
        new(thead) T(src);
        phead=thead;
        ++head;
        ++count;
      }
      T& back()
      {
        return *phead;
      }
      T* alloc()
      {
        push_back(T());
        return phead;
      }

      MemChunk():chunks(0),phead(0),head(0),nelem(Chunk::size/sizeof(T)),count(0)
      {grow();}

      ~MemChunk()
      {
        Chunk *p;
        T *t;
        int i;
        //special handling for first chunk since it's not full
        t=reinterpret_cast<T*>(chunks->mem);
        for (i=0;i<count;i++)
        {
          t[i].~T();
        }
        p=chunks;
        chunks=chunks->next;
        delete p;

        while(chunks!=0)
        {
          t=reinterpret_cast<T*>(chunks->mem);
          for (i=0;i<nelem;i++)
          {
            t[i].~T();
          }
          p=chunks;
          chunks=chunks->next;
          delete p;
        }
      }


    private:
      template<class U> MemChunk(MemChunk<U>&);
      MemChunk* operator=(const MemChunk&);

      struct Chunk {
        enum {size=16*1024-sizeof(Chunk*)-32};
        char mem[size];
        Chunk *next;
      };

      void grow()
      {
        Chunk *c=new Chunk;

        memset(c->mem,0,Chunk::size);

        c->next=chunks;
        count=0;
        chunks=c;
        head=reinterpret_cast<T*>(c->mem);
      };

      Chunk *chunks;
      T *phead, *head;
      const int nelem;
      int count;
  };

}
#endif
