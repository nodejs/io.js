#ifndef SRC_PERMISSION_FS_PERMISSION_H_
#define SRC_PERMISSION_FS_PERMISSION_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "v8.h"

#include <unordered_map>
#include "permission/permission_base.h"
#include "util.h"

namespace node {

namespace permission {

class FSPermission final : public PermissionBase {
 public:
  void Apply(Environment* env,
             const std::vector<std::string>& allow,
             PermissionScope scope) override;
  bool is_granted(Environment* env,
                  PermissionScope perm,
                  const std::string_view& param) const override;

  struct RadixTree {
    struct Node {
      std::string prefix;
      std::unordered_map<char, Node*> children;
      Node* wildcard_child;
      bool is_leaf;

      explicit Node(const std::string& pre)
          : prefix(pre), wildcard_child(nullptr), is_leaf(false) {}

      Node() : wildcard_child(nullptr), is_leaf(false) {}

      Node* CreateChild(const std::string& path_prefix) {
        if (path_prefix.empty() && !is_leaf) {
          is_leaf = true;
          return this;
        }

        CHECK(!path_prefix.empty());
        char label = path_prefix[0];

        Node* child = children[label];
        if (child == nullptr) {
          children[label] = new Node(path_prefix);
          return children[label];
        }

        // swap prefix
        size_t i = 0;
        size_t prefix_len = path_prefix.length();
        for (; i < child->prefix.length(); ++i) {
          if (i > prefix_len || path_prefix[i] != child->prefix[i]) {
            std::string parent_prefix = child->prefix.substr(0, i);
            std::string child_prefix = child->prefix.substr(i);

            child->prefix = child_prefix;
            Node* split_child = new Node(parent_prefix);
            split_child->children[child_prefix[0]] = child;
            children[parent_prefix[0]] = split_child;

            return split_child->CreateChild(path_prefix.substr(i));
          }
        }
        child->is_leaf = true;
        return child->CreateChild(path_prefix.substr(i));
      }

      Node* CreateWildcardChild() {
        if (wildcard_child != nullptr) {
          return wildcard_child;
        }
        wildcard_child = new Node();
        return wildcard_child;
      }

      Node* NextNode(const std::string& path, size_t idx) const {
        if (idx >= path.length()) {
          return nullptr;
        }

        // wildcard node takes precedence
        if (children.size() > 1) {
          auto it = children.find('*');
          if (it != children.end()) {
            return it->second;
          }
        }

        auto it = children.find(path[idx]);
        if (it == children.end()) {
          return nullptr;
        }
        auto child = it->second;
        // match prefix
        size_t prefix_len = child->prefix.length();
        for (size_t i = 0; i < path.length(); ++i) {
          if (i >= prefix_len || child->prefix[i] == '*') {
            return child;
          }

          // Handle optional trailing
          // path = /home/subdirectory
          // child = subdirectory/*
          if (idx >= path.length() &&
              child->prefix[i] == node::kPathSeparator) {
            continue;
          }

          if (path[idx++] != child->prefix[i]) {
            return nullptr;
          }
        }
        return child;
      }

      // A node can be a *end* node and have children
      // E.g: */slower*, */slown* are inserted:
      // /slow
      // ---> er
      // ---> n
      // If */slow* is inserted right after, it will create an
      // empty node
      // /slow
      // ---> '\000' ASCII (0) || \0
      // ---> er
      // ---> n
      bool IsEndNode() const {
        if (children.empty()) {
          return true;
        }
        return is_leaf;
      }
    };

    RadixTree();
    ~RadixTree();
    void Insert(const std::string& s);
    bool Lookup(const std::string_view& s) const { return Lookup(s, false); }
    bool Lookup(const std::string_view& s, bool when_empty_return) const;

   private:
    Node* root_node_;
  };

 private:
  void GrantAccess(PermissionScope scope, const std::string& param);
  // fs granted on startup
  RadixTree granted_in_fs_;
  RadixTree granted_out_fs_;

  bool deny_all_in_ = true;
  bool deny_all_out_ = true;

  bool allow_all_in_ = false;
  bool allow_all_out_ = false;
};

}  // namespace permission

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_PERMISSION_FS_PERMISSION_H_
