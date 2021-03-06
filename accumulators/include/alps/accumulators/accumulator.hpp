/*
 * Copyright (C) 1998-2014 ALPS Collaboration. See COPYRIGHT.TXT
 * All rights reserved. Use is subject to license terms. See LICENSE.TXT
 * For use in publications, see ACKNOWLEDGE.TXT
 */

#ifndef ALPS_ACCUMULATOR_ACCUMULATOR_HPP
#define ALPS_ACCUMULATOR_ACCUMULATOR_HPP

#include <alps/config.hpp>
#include <alps/hdf5/vector.hpp>

#include <alps/accumulators/wrappers.hpp>
// #include <alps/accumulators/feature/weight_holder.hpp>
#include <alps/accumulators/wrapper_set.hpp>

#include <alps/hdf5/archive.hpp>

#include <boost/mpl/if.hpp>
#include <boost/mpl/list.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/mpl/has_key.hpp>
#include <boost/mpl/transform.hpp>
#include <boost/variant/variant.hpp>
#include <boost/variant/get.hpp>
#include <boost/mpl/placeholders.hpp>

#ifdef ALPS_HAVE_MPI
    #include <alps/utilities/boost_mpi.hpp>
#endif

#include <typeinfo>
#include <stdexcept>

namespace alps {
    namespace accumulators {

        namespace detail {
            template<typename T> struct add_base_wrapper_pointer {
                typedef boost::shared_ptr<base_wrapper<T> > type;
            };

            typedef boost::make_variant_over<boost::mpl::transform<
                  boost::mpl::vector<ALPS_ACCUMULATOR_VALUE_TYPES>
                , detail::add_base_wrapper_pointer<boost::mpl::_1> 
            >::type>::type variant_type;

            template<typename T, typename A> struct is_valid_argument : public boost::mpl::if_<
                  typename boost::is_scalar<A>::type
                , typename boost::is_convertible<T, A>::type
                , typename boost::is_same<T, A>::type
            > {};
        }

        // TODO: merge with accumulator_wrapper, at least make common base ...
        class ALPS_DECL result_wrapper {
            public:

                // default constructor
                result_wrapper() 
                    : m_variant()
                {}

                // constructor from raw result
                template<typename T> result_wrapper(T arg)
                    : m_variant(typename detail::add_base_wrapper_pointer<typename value_type<T>::type>::type(
                        new derived_result_wrapper<T>(arg))
                      )
                {}

                // constructor from base_wrapper
                template<typename T> result_wrapper(base_wrapper<T> * arg)
                    : m_variant(typename detail::add_base_wrapper_pointer<T>::type(arg))
                {}

            // copy constructor
            private:
                struct copy_visitor: public boost::static_visitor<> {
                    copy_visitor(detail::variant_type & s): self(s) {}
                    template<typename T> void operator()(T const & arg) const {
                        const_cast<detail::variant_type &>(self) = T(arg->clone());
                    }
                    detail::variant_type & self;
                };
            public:
                result_wrapper(result_wrapper const & rhs)
                    : m_variant()
                {
                    copy_visitor visitor(m_variant);
                    boost::apply_visitor(visitor, rhs.m_variant);
                }

                // constructor from hdf5
                result_wrapper(hdf5::archive & ar) {
                    ar[""] >> *this;
                }

            // operator=
            private:
                struct assign_visitor: public boost::static_visitor<> {
                    assign_visitor(result_wrapper * s): self(s) {}
                    template<typename T> void operator()(T & arg) const {
                        self->m_variant = arg;
                    }
                    mutable result_wrapper * self;
                };
            public:
                result_wrapper & operator=(boost::shared_ptr<result_wrapper> const & rhs) {
                    boost::apply_visitor(assign_visitor(this), rhs->m_variant);
                    return *this;
                }

            // get
            private:
                template<typename T> struct get_visitor: public boost::static_visitor<> {
                    template<typename X> void operator()(X const & arg) {
                        throw std::runtime_error(std::string("Canot cast observable") + typeid(X).name() + " to base type: " + typeid(T).name() + ALPS_STACKTRACE);
                    }
                    void operator()(typename detail::add_base_wrapper_pointer<T>::type const & arg) { value = arg; }
                    typename detail::add_base_wrapper_pointer<T>::type value;
                };
            public:
                template <typename T> base_wrapper<T> & get() {
                    get_visitor<T> visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return *visitor.value;
                }

            // extract
            private:
                template<typename A> struct extract_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T const & arg) { value = &arg->template extract<A>(); }
                    A * value;
                };
            public:
                template <typename A> A & extract() {
                    extract_visitor<A> visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return *visitor.value;
                }
                template <typename A> A const & extract() const {
                    extract_visitor<A> visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return *visitor.value;
                }

            // count
            private:
                struct count_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T const & arg) const { value = arg->count(); }
                    mutable boost::uint64_t value;
                };
            public:
                boost::uint64_t count() const {
                    count_visitor visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return visitor.value;
                }

            // mean, error
            #define ALPS_ACCUMULATOR_PROPERTY_PROXY(PROPERTY, TYPE)                                                 \
                private:                                                                                            \
                    template<typename T> struct PROPERTY ## _visitor: public boost::static_visitor<> {              \
                        template<typename X> void apply(typename boost::enable_if<                                  \
                            typename detail::is_valid_argument<typename TYPE <X>::type, T>::type, X const &         \
                        >::type arg) const {                                                                        \
                            value = arg. PROPERTY ();                                                               \
                        }                                                                                           \
                        template<typename X> void apply(typename boost::disable_if<                                 \
                            typename detail::is_valid_argument<typename TYPE <X>::type, T>::type, X const &         \
                        >::type arg) const {                                                                        \
                            throw std::logic_error(std::string("cannot convert: ")                                  \
                                + typeid(typename TYPE <X>::type).name() + " to "                                   \
                                + typeid(T).name() + ALPS_STACKTRACE);                                              \
                        }                                                                                           \
                        template<typename X> void operator()(X const & arg) const {                                 \
                            apply<typename X::element_type>(*arg);                                                  \
                        }                                                                                           \
                        mutable T value;                                                                            \
                    };                                                                                              \
                public:                                                                                             \
                    template<typename T> typename TYPE <base_wrapper<T> >::type PROPERTY () const {                 \
                        PROPERTY ## _visitor<typename TYPE <base_wrapper<T> >::type> visitor;                       \
                        boost::apply_visitor(visitor, m_variant);                                                   \
                        return visitor.value;                                                                       \
                    }
            ALPS_ACCUMULATOR_PROPERTY_PROXY(mean, mean_type)
            ALPS_ACCUMULATOR_PROPERTY_PROXY(error, error_type)
            #undef ALPS_ACCUMULATOR_PROPERTY_PROXY

            // save
            private:
                struct save_visitor: public boost::static_visitor<> {
                    save_visitor(hdf5::archive & a): ar(a) {}
                    template<typename T> void operator()(T & arg) const { ar[""] = *arg; }
                    hdf5::archive & ar;
                };
            public:
                void save(hdf5::archive & ar) const {
                    boost::apply_visitor(save_visitor(ar), m_variant);
                }

            // load
            private:
                struct load_visitor: public boost::static_visitor<> {
                    load_visitor(hdf5::archive & a): ar(a) {}
                    template<typename T> void operator()(T const & arg) const { ar[""] >> *arg; }
                    hdf5::archive & ar;
                };
            public:
                void load(hdf5::archive & ar) {
                    boost::apply_visitor(load_visitor(ar), m_variant);
                }

            // print
            private:
                struct print_visitor: public boost::static_visitor<> {
                    print_visitor(std::ostream & o): os(o) {}
                    template<typename T> void operator()(T const & arg) const { arg->print(os); }
                    std::ostream & os;
                };
            public:
                void print(std::ostream & os) const {
                    boost::apply_visitor(print_visitor(os), m_variant);
                }

            // transform(T F(T))
            private:
                template<typename T> struct transform_1_visitor: public boost::static_visitor<> {
                    transform_1_visitor(boost::function<T(T)> f) : op(f) {}
                    template<typename X> void apply(typename boost::enable_if<
                        typename detail::is_valid_argument<T, typename value_type<X>::type>::type, X &
                    >::type arg) const {
                        arg.transform(op);
                    }
                    template<typename X> void apply(typename boost::disable_if<
                        typename detail::is_valid_argument<T, typename value_type<X>::type>::type, X &
                    >::type arg) const {
                        throw std::logic_error(std::string("cannot convert: ") + typeid(T).name() + " to " + typeid(typename value_type<X>::type).name() + ALPS_STACKTRACE);
                    }
                    template<typename X> void operator()(X & arg) const {
                        apply<typename X::element_type>(*arg);
                    }
                    boost::function<T(T)> op;
                };
            public:
                template<typename T> result_wrapper transform(boost::function<T(T)> op) const {
                    result_wrapper clone(*this);
                    boost::apply_visitor(transform_1_visitor<T>(op), clone.m_variant);
                    return clone;
                }
                template<typename T> result_wrapper transform(T(*op)(T)) const {
                    return transform(boost::function<T(T)>(op));
                }

            // unary plus
            public:
                result_wrapper operator+ () const {
                    return result_wrapper(*this);
                }

            // unary minus
            private:
                struct unary_add_visitor: public boost::static_visitor<> {
                    template<typename X> void operator()(X & arg) const {
                        arg->negate();
                    }
                };
            public:
                result_wrapper operator- () const {
                    result_wrapper clone(*this);
                    unary_add_visitor visitor;
                    boost::apply_visitor(visitor, clone.m_variant);
                    return clone;
                }

            // operators
            #define ALPS_ACCUMULATOR_OPERATOR_PROXY(OPNAME, AUGOPNAME, AUGOP, FUN)                                  \
                private:                                                                                            \
                    template<typename T> struct FUN ## _arg_visitor: public boost::static_visitor<> {               \
                        FUN ## _arg_visitor(T & v): value(v) {}                                                     \
                        template<typename X> void apply(X const &) const {                                          \
                            throw std::logic_error("only results with equal value types are allowed in operators"   \
                                + ALPS_STACKTRACE);                                                                 \
                        }                                                                                           \
                        void apply(T const & arg) const {                                                           \
                            const_cast<T &>(value) AUGOP arg;                                                       \
                        }                                                                                           \
                        template<typename X> void operator()(X const & arg) const {                                 \
                            apply(*arg);                                                                            \
                        }                                                                                           \
                        T & value;                                                                                  \
                    };                                                                                              \
                    struct FUN ## _self_visitor: public boost::static_visitor<> {                                   \
                        FUN ## _self_visitor(result_wrapper const & v): value(v) {}                                 \
                        template<typename X> void operator()(X & self) const {                                      \
                            FUN ## _arg_visitor<typename X::element_type> visitor(*self);                           \
                            boost::apply_visitor(visitor, value.m_variant);                                         \
                        }                                                                                           \
                        result_wrapper const & value;                                                               \
                    };                                                                                              \
                    struct FUN ## _double_visitor: public boost::static_visitor<> {                                 \
                        FUN ## _double_visitor(double v): value(v) {}                                               \
                        template<typename X> void operator()(X & arg) const {                                       \
                            *arg AUGOP value;                                                                       \
                        }                                                                                           \
                        double value;                                                                               \
                    };                                                                                              \
                public:                                                                                             \
                    result_wrapper & AUGOPNAME (result_wrapper const & arg) {                                       \
                        FUN ## _self_visitor visitor(arg);                                                          \
                        boost::apply_visitor(visitor, m_variant);                                                   \
                        return *this;                                                                               \
                    }                                                                                               \
                    result_wrapper & AUGOPNAME (double arg) {                                                       \
                        FUN ## _double_visitor visitor(arg);                                                        \
                        boost::apply_visitor(visitor, m_variant);                                                   \
                        return *this;                                                                               \
                    }                                                                                               \
                    result_wrapper OPNAME (result_wrapper const & arg) const {                                      \
                        result_wrapper clone(*this);                                                                \
                        clone AUGOP arg;                                                                            \
                        return clone;                                                                               \
                    }                                                                                               \
                    result_wrapper OPNAME (double arg) const {                                                      \
                        result_wrapper clone(*this);                                                                \
                        clone AUGOP arg;                                                                            \
                        return clone;                                                                               \
                    }
            ALPS_ACCUMULATOR_OPERATOR_PROXY(operator+, operator+=, +=, add)
            ALPS_ACCUMULATOR_OPERATOR_PROXY(operator-, operator-=, -=, sub)
            ALPS_ACCUMULATOR_OPERATOR_PROXY(operator*, operator*=, *=, mul)
            ALPS_ACCUMULATOR_OPERATOR_PROXY(operator/, operator/=, /=, div)
            #undef ALPS_ACCUMULATOR_OPERATOR_PROXY

            // inverse
            private:
                struct inverse_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T & arg) const { arg->inverse(); }
                };
            public:
                result_wrapper inverse() const {
                    result_wrapper clone(*this);
                    boost::apply_visitor(inverse_visitor(), clone.m_variant);
                    return clone;
                }

            #define ALPS_ACCUMULATOR_FUNCTION_PROXY(FUN)                                \
                private:                                                                \
                    struct FUN ## _visitor: public boost::static_visitor<> {            \
                        template<typename T> void operator()(T & arg) const {           \
                            arg-> FUN ();                                               \
                        }                                                               \
                    };                                                                  \
                public:                                                                 \
                    result_wrapper FUN () const {                                       \
                        result_wrapper clone(*this);                                    \
                        boost::apply_visitor( FUN ## _visitor(), clone.m_variant);      \
                        return clone;                                                   \
                    }
            ALPS_ACCUMULATOR_FUNCTION_PROXY(sin)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(cos)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(tan)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(sinh)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(cosh)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(tanh)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(asin)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(acos)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(atan)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(abs)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(sqrt)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(log)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(sq)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(cb)
            ALPS_ACCUMULATOR_FUNCTION_PROXY(cbrt)
            #undef ALPS_ACCUMULATOR_FUNCTION_PROXY

            private:

                detail::variant_type m_variant;
        };
        inline result_wrapper operator+(double arg1, result_wrapper const & arg2) {
            return arg2 + arg1;
        }
        inline result_wrapper operator-(double arg1, result_wrapper const & arg2) {
            return -arg2 + arg1;
        }
        inline result_wrapper operator*(double arg1, result_wrapper const & arg2) {
            return arg2 * arg1;
        }
        inline result_wrapper operator/(double arg1, result_wrapper const & arg2) {
            return arg2.inverse() * arg1;
        }

        inline std::ostream & operator<<(std::ostream & os, const result_wrapper & arg) {
            arg.print(os);
            return os;
        }

        template <typename A> A & extract(result_wrapper & m) {
            return m.extract<A>();
        }

        #define EXTERNAL_FUNCTION(FUN)                          \
            result_wrapper FUN (result_wrapper const & arg);

            EXTERNAL_FUNCTION(sin)
            EXTERNAL_FUNCTION(cos)
            EXTERNAL_FUNCTION(tan)
            EXTERNAL_FUNCTION(sinh)
            EXTERNAL_FUNCTION(cosh)
            EXTERNAL_FUNCTION(tanh)
            EXTERNAL_FUNCTION(asin)
            EXTERNAL_FUNCTION(acos)
            EXTERNAL_FUNCTION(atan)
            EXTERNAL_FUNCTION(abs)
            EXTERNAL_FUNCTION(sqrt)
            EXTERNAL_FUNCTION(log)
            EXTERNAL_FUNCTION(sq)
            EXTERNAL_FUNCTION(cb)
            EXTERNAL_FUNCTION(cbrt)

        #undef EXTERNAL_FUNCTION

        class ALPS_DECL accumulator_wrapper {
            private:
                /// Safety check: verify that a pointer is valid.
                // FIXME: better initialize the pointer with something reasonable to begin with?
                template <typename T>
                static void check_ptr(const boost::shared_ptr<T>& ptr) {
                    if (!ptr) throw std::runtime_error("Uninitialized accumulator accessed");
                }
            public:
            // default constructor
                accumulator_wrapper() 
                    : m_variant()
                {}

            // constructor from raw accumulator
                template<typename T> accumulator_wrapper(T arg)
                    : m_variant(typename detail::add_base_wrapper_pointer<typename value_type<T>::type>::type(
                        new derived_accumulator_wrapper<T>(arg))
                      )
                {}

            // copy constructor
                accumulator_wrapper(accumulator_wrapper const & rhs)
                    : m_variant(rhs.m_variant)
                {}

            // constructor from hdf5
                accumulator_wrapper(hdf5::archive & ar) {
                    ar[""] >> *this;
                }

            // operator(T)
            private:
                template<typename T> struct call_1_visitor: public boost::static_visitor<> {
                    call_1_visitor(T const & v) : value(v) {}
                    template<typename X> void apply(typename boost::enable_if<
                        typename detail::is_valid_argument<T, typename value_type<X>::type>::type, X &
                    >::type arg) const {
                        arg(value); 
                    }
                    template<typename X> void apply(typename boost::disable_if<
                        typename detail::is_valid_argument<T, typename value_type<X>::type>::type, X &
                    >::type arg) const {
                        throw std::logic_error(std::string("cannot convert: ") + typeid(T).name() + " to " + typeid(typename value_type<X>::type).name() + ALPS_STACKTRACE);
                    }
                    template<typename X> void operator()(X & arg) const {
                        check_ptr(arg);
                        apply<typename X::element_type>(*arg);
                    }
                    T const & value;
                };
            public:
                template<typename T> void operator()(T const & value) {
                    boost::apply_visitor(call_1_visitor<T>(value), m_variant);
                }
                template<typename T> accumulator_wrapper & operator<<(T const & value) {
                    (*this)(value);
                    return (*this);
                }

                // Code to merge accumulators
            private:
                /// Service class to access elements of a variant type
                struct merge_visitor: public boost::static_visitor<> {
                  // The accumulator we want to merge (RHS):
                  const accumulator_wrapper& rhs_acc;

                  // Remember the RHS accumulator
                  merge_visitor(const accumulator_wrapper& b): rhs_acc(b) {}
                  
                  // This is called by apply_visitor()
                  template <typename P> // P can be dereferenced to base_wrapper<T>
                  void operator()(P& lhs_ptr)
                  {
                    const P* rhs_ptr=boost::get<P>(& rhs_acc.m_variant);
                    if (!rhs_ptr) throw std::runtime_error("Only accumulators of the same type can be merged"
                                                           + ALPS_STACKTRACE);
                    check_ptr(*rhs_ptr);
                    lhs_ptr->merge(**rhs_ptr);
                  }
                };
            public:
                /// Merge another accumulator into this one. @param rhs_acc  accumulator to merge.
                void merge(const accumulator_wrapper& rhs_acc) {
                  merge_visitor visitor(rhs_acc);
                  boost::apply_visitor(visitor, m_variant);
                }

            // // operator(T, W)
            // private:
            //     template<typename T, typename W> struct call_2_visitor: public boost::static_visitor<> {
            //         call_2_visitor(T const & v, W const & w) : value(v), weight(w) {}
            //         template<typename X> void apply(typename boost::enable_if<
            //             typename detail::is_valid_argument<T, typename value_type<X>::type>::type, X &
            //         >::type arg) const {
            //             arg(value, weight);
            //         }
            //         template<typename X> void apply(typename boost::disable_if<
            //             typename detail::is_valid_argument<T, typename value_type<X>::type>::type, X &
            //         >::type arg) const {
            //             throw std::logic_error(std::string("cannot convert: ") + typeid(T).name() + " to " + typeid(typename value_type<X>::type).name() + ALPS_STACKTRACE);
            //         }
            //         template<typename X> void operator()(X & arg) const {
            //             apply<typename X::element_type>(*arg);
            //         }
            //         T const & value;
            //         detail::weight_variant_type weight;
            //     };
            // public:
            //     template<typename T, typename W> void operator()(T const & value, W const & weight) {
            //         boost::apply_visitor(call_2_visitor<T, W>(value, weight), m_variant);
            //     }

            // operator=
            private:
                struct assign_visitor: public boost::static_visitor<> {
                    assign_visitor(accumulator_wrapper * s): self(s) {}
                    template<typename T> void operator()(T & arg) const {
                        self->m_variant = arg;
                    }
                    mutable accumulator_wrapper * self;
                };
            public:
                accumulator_wrapper & operator=(boost::shared_ptr<accumulator_wrapper> const & rhs) {
                    boost::apply_visitor(assign_visitor(this), rhs->m_variant);
                    return *this;
                }

            // get
            private:
                template<typename T> struct get_visitor: public boost::static_visitor<> {
                    template<typename X> void operator()(X const & arg) {
                        throw std::runtime_error(std::string("Canot cast observable") + typeid(X).name() + " to base type: " + typeid(T).name() + ALPS_STACKTRACE);
                    }
                    void operator()(typename detail::add_base_wrapper_pointer<T>::type const & arg) { value = arg; }
                    typename detail::add_base_wrapper_pointer<T>::type value;
                };
            public:
                template <typename T> base_wrapper<T> & get() {
                    get_visitor<T> visitor;
                    boost::apply_visitor(visitor, m_variant);
                    check_ptr(visitor.value);
                    return *visitor.value;
                }

            // extract
            private:
                template<typename A> struct extract_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T const & arg) { check_ptr(arg); value = &arg->template extract<A>(); }
                    A * value;
                };
            public:
                template <typename A> A & extract() {
                    extract_visitor<A> visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return *visitor.value;
                }

            // count
            private:
                struct count_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T const & arg) const { check_ptr(arg); value = arg->count(); }
                    mutable boost::uint64_t value;
                };
            public:
                boost::uint64_t count() const {
                    count_visitor visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return visitor.value;
                }

            // mean, error
            #define ALPS_ACCUMULATOR_PROPERTY_PROXY(PROPERTY, TYPE)                                                 \
                private:                                                                                            \
                    template<typename T> struct PROPERTY ## _visitor: public boost::static_visitor<> {              \
                        template<typename X> void apply(typename boost::enable_if<                                  \
                            typename detail::is_valid_argument<typename TYPE <X>::type, T>::type, X const &         \
                        >::type arg) const {                                                                        \
                            value = arg. PROPERTY ();                                                               \
                        }                                                                                           \
                        template<typename X> void apply(typename boost::disable_if<                                 \
                            typename detail::is_valid_argument<typename TYPE <X>::type, T>::type, X const &         \
                        >::type arg) const {                                                                        \
                            throw std::logic_error(std::string("cannot convert: ")                                  \
                                + typeid(typename TYPE <X>::type).name() + " to "                                   \
                                + typeid(T).name() + ALPS_STACKTRACE);                                              \
                        }                                                                                           \
                        template<typename X> void operator()(X const & arg) const {                                 \
                            check_ptr(arg);                                                                         \
                            apply<typename X::element_type>(*arg);                                                  \
                        }                                                                                           \
                        mutable T value;                                                                            \
                    };                                                                                              \
                public:                                                                                             \
                    template<typename T> typename TYPE <base_wrapper<T> >::type PROPERTY () const {                 \
                        PROPERTY ## _visitor<typename TYPE <base_wrapper<T> >::type> visitor;                       \
                        boost::apply_visitor(visitor, m_variant);                                                   \
                        return visitor.value;                                                                       \
                    }
            ALPS_ACCUMULATOR_PROPERTY_PROXY(mean, mean_type)
            ALPS_ACCUMULATOR_PROPERTY_PROXY(error, error_type)
            #undef ALPS_ACCUMULATOR_PROPERTY_PROXY

            // save
            private:
                struct save_visitor: public boost::static_visitor<> {
                    save_visitor(hdf5::archive & a): ar(a) {}
                    template<typename T> void operator()(T & arg) const { check_ptr(arg); ar[""] = *arg; }
                    hdf5::archive & ar;
                };
            public:
                void save(hdf5::archive & ar) const {
                    boost::apply_visitor(save_visitor(ar), m_variant);
                }

            // load
            private:
                struct load_visitor: public boost::static_visitor<> {
                    load_visitor(hdf5::archive & a): ar(a) {}
                    template<typename T> void operator()(T const & arg) const { check_ptr(arg); ar[""] >> *arg; }
                    hdf5::archive & ar;
                };
            public:
                void load(hdf5::archive & ar) {
                    boost::apply_visitor(load_visitor(ar), m_variant);
                }

            // reset
            private:
                struct reset_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T const & arg) const { check_ptr(arg); arg->reset(); }
                };
            public:
                void reset() const {
                    boost::apply_visitor(reset_visitor(), m_variant);
                }

            // result
            private:
                struct result_visitor: public boost::static_visitor<> {
                    template<typename T> void operator()(T const & arg) {
                        check_ptr(arg);
                        value = boost::shared_ptr<result_wrapper>(new result_wrapper(arg->result()));
                    }
                    boost::shared_ptr<result_wrapper> value;
                };
            public:
                boost::shared_ptr<result_wrapper> result() const {
                    result_visitor visitor;
                    boost::apply_visitor(visitor, m_variant);
                    return visitor.value;
                }

            // print
            private:
                struct print_visitor: public boost::static_visitor<> {
                    print_visitor(std::ostream & o): os(o) {}
                    template<typename T> void operator()(T const & arg) const { check_ptr(arg); arg->print(os); }
                    std::ostream & os;
                };
            public:
                void print(std::ostream & os) const {
                    boost::apply_visitor(print_visitor(os), m_variant);
                }

#ifdef ALPS_HAVE_MPI
            // collective_merge
            private:
                struct collective_merge_visitor: public boost::static_visitor<> {
                    collective_merge_visitor(boost::mpi::communicator const & c, int r): comm(c), root(r) {}
                    template<typename T> void operator()(T & arg) const { arg->collective_merge(comm, root); }
                    template<typename T> void operator()(T const & arg) const { arg->collective_merge(comm, root); }
                    boost::mpi::communicator const & comm;
                    int root;
                };
            public:
                inline void collective_merge(boost::mpi::communicator const & comm, int root) {
                    boost::apply_visitor(collective_merge_visitor(comm, root), m_variant);
                }
                inline void collective_merge(boost::mpi::communicator const & comm, int root) const {
                    boost::apply_visitor(collective_merge_visitor(comm, root), m_variant);
                }
#endif

            private:

                detail::variant_type m_variant;
        };

        inline std::ostream & operator<<(std::ostream & os, const accumulator_wrapper & arg) {
            arg.print(os);
            return os;
        }

        template <typename A> A & extract(accumulator_wrapper & m) {
            return m.extract<A>();
        }

        inline void ALPS_DECL reset(accumulator_wrapper & arg) {
            return arg.reset();
        }

        typedef impl::wrapper_set<accumulator_wrapper> accumulator_set;
        typedef impl::wrapper_set<result_wrapper> result_set;

    }
}

 #endif
