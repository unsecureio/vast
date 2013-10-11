#include "vast/event_index.h"

#include "vast/bitmap_index/address.h"
#include "vast/bitmap_index/arithmetic.h"
#include "vast/bitmap_index/port.h"
#include "vast/convert.h"

using namespace cppa;

namespace vast {

struct event_meta_index::querier : expr::default_const_visitor
{
  querier(event_meta_index const& idx)
    : idx{idx}
  {
  }

  virtual void visit(expr::constant const& c)
  {
    val = &c.val;
  }

  virtual void visit(expr::relation const& rel)
  {
    op = &rel.op;
    rel.operands[1]->accept(*this);
    rel.operands[0]->accept(*this);
  }

  virtual void visit(expr::name_extractor const&)
  {
    assert(op);
    assert(val);
    if (auto r = idx.name_.lookup(*op, *val))
      result = std::move(*r);
  }

  virtual void visit(expr::timestamp_extractor const&)
  {
    assert(op);
    assert(val);
    if (auto r = idx.timestamp_.lookup(*op, *val))
      result = std::move(*r);
  }

  virtual void visit(expr::id_extractor const&)
  {
    assert(! "not yet implemented");
  }

  bitstream result;
  event_meta_index const& idx;
  value const* val = nullptr;
  relational_operator const* op = nullptr;
};


event_meta_index::event_meta_index(path dir)
  : event_index<event_meta_index>{std::move(dir)}
{
  // ID 0 is not a valid event.
  timestamp_.append(1, false);
  name_.append(1, false);
}

char const* event_meta_index::description() const
{
  return "event-meta-index";
}

void event_meta_index::load()
{
  io::unarchive(dir_ / "timestamp.idx", timestamp_);
  io::unarchive(dir_ / "name.idx", name_);
  VAST_LOG_ACTOR_DEBUG(
      "loaded timestamp/name index with " <<
      timestamp_.size() - 1 << '/' << name_.size() - 1 << " events");
}

void event_meta_index::store()
{
  io::archive(dir_ / "timestamp.idx", timestamp_);
  io::archive(dir_ / "name.idx", name_);
  VAST_LOG_ACTOR_DEBUG(
      "stored timestamp/name index with " <<
      timestamp_.size() - 1 << '/' << name_.size() - 1 << " events");
}

bool event_meta_index::index(event const& e)
{
  return timestamp_.push_back(e.timestamp(), e.id())
      && name_.push_back(e.name(), e.id());
}

bitstream event_meta_index::lookup(expr::ast const& ast) const
{
  querier visitor{*this};
  ast.accept(visitor);
  return std::move(visitor.result);
}


struct event_arg_index::querier : expr::default_const_visitor
{
  querier(event_arg_index const& idx)
    : idx{idx}
  {
  }

  virtual void visit(expr::constant const& c)
  {
    val = &c.val;
  }

  virtual void visit(expr::relation const& rel)
  {
    op = &rel.op;
    rel.operands[1]->accept(*this);
    rel.operands[0]->accept(*this);
  }

  virtual void visit(expr::offset_extractor const& oe)
  {
    assert(op);
    assert(val);
    auto i = idx.args_.find(oe.off);
    if (i == idx.args_.end())
      return;
    if (auto r = i->second->lookup(*op, *val))
      result = std::move(*r);
  }

  virtual void visit(expr::type_extractor const& te)
  {
    assert(op);
    assert(val);
    assert(te.type == val->which());
    auto i = idx.types_.find(te.type);
    if (i == idx.types_.end())
      return;
    for (auto& bmi : i->second)
      if (auto r = bmi->lookup(*op, *val))
      {
        if (result)
          result |= bitstream{std::move(*r)};
        else
          result = std::move(*r);
      }
  }

  bitstream result;
  event_arg_index const& idx;
  value const* val = nullptr;
  relational_operator const* op = nullptr;
};


event_arg_index::event_arg_index(path dir)
  : event_index<event_arg_index>{std::move(dir)}
{
}

char const* event_arg_index::description() const
{
  return "event-arg-index";
}

void event_arg_index::load()
{
  std::set<path> paths;
  traverse(dir_, [&](path const& p) -> bool { paths.insert(p); return true; });
  for (auto& p : paths)
  {
    offset o;
    auto str = p.basename(true).str().substr(1);
    auto start = str.begin();
    if (! extract(start, str.end(), o))
    {
      VAST_LOG_ACTOR_ERROR("got invalid offset: " << p.basename());
      quit(exit::error);
      return;
    }

    value_type vt;
    std::shared_ptr<bitmap_index> bmi;
    io::unarchive(p, vt, bmi);
    if (! bmi)
    {
      VAST_LOG_ACTOR_ERROR("got corrupt index: " << p.basename());
      quit(exit::error);
      return;
    }
    VAST_LOG_ACTOR_DEBUG("loaded index " << p.trim(-3) << " with " <<
                         bmi->size() - 1<< " events");
    args_.emplace(o, bmi);
    types_[vt].push_back(bmi);
  }
}

void event_arg_index::store()
{
  VAST_LOG_ACTOR_DEBUG("saves indexes to filesystem");

  std::map<std::shared_ptr<bitmap_index>, value_type> inverse;
  for (auto& p : types_)
    for (auto& bmi : p.second)
      if (inverse.find(bmi) == inverse.end())
        inverse.emplace(bmi, p.first);

  static string prefix{"@"};
  static string suffix{".idx"};
  for (auto& p : args_)
  {
    if (p.second->empty())
      continue;
    path const filename = dir_ / (prefix + to<string>(p.first) + suffix);
    assert(inverse.count(p.second));
    io::archive(filename, inverse[p.second], p.second);
    VAST_LOG_ACTOR_DEBUG("stored index " << filename.trim(-3) <<
                         " with " << p.second->size() - 1 << " events");
  }
}

bool event_arg_index::index(event const& e)
{
  if (e.empty())
    return true;
  offset o{0};
  return index_record(e, e.id(), o);
}

bitstream event_arg_index::lookup(expr::ast const& ast) const
{
  querier visitor{*this};
  ast.accept(visitor);
  return std::move(visitor.result);
}

bool event_arg_index::index_record(record const& r, uint64_t id, offset& o)
{
  if (o.empty())
    return true;
  for (auto& v : r)
  {
    if (v.which() == record_type && v)
    {
      auto& inner = v.get<record>();
      if (! inner.empty())
      {
        o.push_back(0);
        if (! index_record(inner, id, o))
          return false;
        o.pop_back();
      }
    }
    else if (! v.invalid() && v.which() != table_type)
    {
      bitmap_index* idx;
      auto i = args_.find(o);
      if (i != args_.end())
      {
        idx = i->second.get();
      }
      else
      {
        auto unique = bitmap_index::create(v.which());
        auto bmi = std::shared_ptr<bitmap_index>{unique.release()};
        idx = bmi.get();
        idx->append(1, false); // ID 0 is not a valid event.
        args_.emplace(o, bmi);
        types_[v.which()].push_back(bmi);
      }
      assert(idx != nullptr);
      if (! idx->push_back(v, id))
        return false;
    }
    ++o.back();
  }
  return true;
}

} // namespace vast
