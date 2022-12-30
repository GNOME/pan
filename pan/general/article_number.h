#ifndef article_number_h
#define article_number_h

#include <stdint.h>
#include <iosfwd>

namespace pan {

struct StringView;

/** This is a count of articles. It isn't the same thing as an article number,
 * even if the underlying type is the same.
 */
class Article_Count
{
  public:
    typedef uint64_t type;

    Article_Count() : val_(0) {}

    explicit Article_Count(type x) : val_(x) {}

    explicit Article_Count(StringView const &);

    Article_Count(Article_Count const &rhs) = default;

    explicit operator type () const { return val_; }

    Article_Count &operator=(Article_Count const &val)
    {
      val_ = val.val_;
      return *this;
    }

    Article_Count operator+(type n) const
    {
      Article_Count res;
      res.val_ = val_ + n;
      return res;
    }

    Article_Count operator-(type n) const
    {
      Article_Count res;
      res.val_ = val_ - n;
      return res;
    }

    Article_Count & operator-=(Article_Count const &rhs)
    {
        val_ -= rhs.val_;
        return *this;
    }

    Article_Count & operator+=(Article_Count const &rhs)
    {
        val_ += rhs.val_;
        return *this;
    }

    Article_Count & operator+=(type const &rhs)
    {
        val_ += rhs;
        return *this;
    }

    Article_Count & operator++()
    {
      val_ += 1;
      return *this;
    }

    Article_Count operator-(Article_Count const &rhs) const
    {
      Article_Count res(val_);
      return res - rhs.val_;
    }

  private:
    type val_;
};

inline bool operator==(Article_Count const &lhs, Article_Count const &rhs)
{
  return static_cast<Article_Count::type>(lhs) == static_cast<Article_Count::type>(rhs);
}

inline bool operator!=(Article_Count const &lhs, Article_Count const &rhs)
{
  return static_cast<Article_Count::type>(lhs) != static_cast<Article_Count::type>(rhs);
}

inline bool operator>=(Article_Count const &lhs, Article_Count const &rhs)
{
  return static_cast<Article_Count::type>(lhs) >= static_cast<Article_Count::type>(rhs);
}

inline bool operator<=(Article_Count const &lhs, Article_Count const &rhs)
{
  return static_cast<Article_Count::type>(lhs) <= static_cast<Article_Count::type>(rhs);
}

inline bool operator<(Article_Count const &lhs, Article_Count const &rhs)
{
  return static_cast<Article_Count::type>(lhs) < static_cast<Article_Count::type>(rhs);
}

inline bool operator>(Article_Count const &lhs, Article_Count const &rhs)
{
  return static_cast<Article_Count::type>(lhs) > static_cast<Article_Count::type>(rhs);
}

std::ostream &operator<<(std::ostream &os, Article_Count a);

/** An article number needs a specific size.
 * Some groups can have way over 4 billion articles.
 */
class Article_Number {
  public:
    typedef uint64_t type;
    Article_Number() : val_(0) {}
    explicit Article_Number(type x) : val_(x) {}
    explicit Article_Number(StringView const &);

    Article_Number(Article_Number const &rhs) = default;

    explicit operator type () const { return val_; }

    Article_Number &operator=(Article_Number const &val)
    {
      val_ = val.val_;
      return *this;
    }

    Article_Number operator+(type n) const
    {
      Article_Number res;
      res.val_ = val_ + n;
      return res;
    }

    Article_Number operator-(type n) const
    {
      Article_Number res;
      res.val_ = val_ - n;
      return res;
    }

    Article_Count operator-(Article_Number const &rhs) const
    {
      Article_Count res(val_);
      return res - rhs.val_;
    }

    Article_Number &operator+=(type n)
    {
      val_ += n;
      return *this;
    }

  private:
    type val_;
};

inline bool operator==(Article_Number const &lhs, Article_Number const &rhs)
{
  return static_cast<Article_Number::type>(lhs) == static_cast<Article_Number::type>(rhs);
}

inline bool operator!=(Article_Number const &lhs, Article_Number const &rhs)
{
  return static_cast<Article_Number::type>(lhs) != static_cast<Article_Number::type>(rhs);
}

inline bool operator<(Article_Number const &lhs, Article_Number const &rhs)
{
  return static_cast<Article_Number::type>(lhs) < static_cast<Article_Number::type>(rhs);
}

inline bool operator<=(Article_Number const &lhs, Article_Number const &rhs)
{
  return static_cast<Article_Number::type>(lhs) <= static_cast<Article_Number::type>(rhs);
}

inline bool operator>(Article_Number const &lhs, Article_Number const &rhs)
{
  return static_cast<Article_Number::type>(lhs) > static_cast<Article_Number::type>(rhs);
}

inline bool operator>=(Article_Number const &lhs, Article_Number const &rhs)
{
  return static_cast<Article_Number::type>(lhs) >= static_cast<Article_Number::type>(rhs);
}

std::ostream &operator<<(std::ostream &os, Article_Number a);

}

#endif
