#include "config.hpp"
#include "explain.hpp"

#include <iostream>
#include <string>

namespace program
{
    struct mantissa_builder
    {
        void
        notify_negative()
        {
            buffer += "-";
        }
        void
        notify_decimal()
        {
            buffer += ".";
        }
        void
        notify_digit(char c)
        {
            buffer += c;
        }
        void
        finalise()
        {
            if (buffer.empty())
                buffer = "0";
        }

        std::string buffer;
    };

    bool
    operator==(mantissa_builder const &l, mantissa_builder const &r)
    {
        return l.buffer == r.buffer;
    }

    struct exponent_builder
    {
        void
        notify_digit(char c)
        {
            buffer += c;
        }
        void
        notify_negative()
        {
            buffer += "-";
        }
        void
        finalise()
        {
            if (buffer.empty())
                buffer = "0";
            buffer.insert(buffer.begin(), 'e');
        }

        std::string buffer;
    };

    bool
    operator==(exponent_builder const &l, exponent_builder const &r)
    {
        return l.buffer == r.buffer;
    }

    struct number
    {
        mantissa_builder mantissa;
        exponent_builder exponent;

        friend std::ostream &
        operator<<(std::ostream &os, number const &n)
        {
            os << n.mantissa.buffer << n.exponent.buffer;
            return os;
        }

        auto
        as_tuple() const
        {
            return std::tie(mantissa, exponent);
        }
    };
    bool
    operator==(number const &l, number const &r)
    {
        return l.as_tuple() == r.as_tuple();
    }

    /// state machine controlling the parsing of a JSON number
    /// given np is an instance of number_parser:
    /// while there is input
    ///   next = np(begin, end);
    struct number_parser : asio::coroutine
    {
        using iterator       = char *;
        using const_iterator = const char *;

        system::error_code const &
        error() const
        {
            return error_;
        }

#include <boost/asio/yield.hpp>
        const_iterator
        operator()(const_iterator begin, const_iterator end)
        {
            auto p = begin;

            auto exhausted = [&] { return p == end; };

            auto consume = [&] {
                ++p;
                return exhausted();
            };

            auto is_digit = [&] {
                auto c = *p;
                return c >= '0' && c <= '9';
            };

            auto finalising = [&] { return begin == end; };

            reenter(this)
            {
                if (finalising())
                {
                    error_ = asio::error::invalid_argument;
                    yield break;
                }
                // [+-]?
                if (*p == '+')
                {
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            error_ = asio::error::invalid_argument;
                            yield break;
                        }
                    }
                }
                else if (*p == '-')
                {
                    mantissa_.notify_negative();
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            error_ = asio::error::invalid_argument;
                            yield break;
                        }
                    }
                }
                // leading zero must be followed by a . or nothing
                if (*p == '0')
                {
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            yield break;
                        }
                    }
                    if (*p != '.')
                    {
                        error_ = asio::error::invalid_argument;
                        yield break;
                    }
                    goto on_mantissa_decimal;
                }
                // keep consuming leading digits
                while (is_digit())
                {
                    mantissa_.notify_digit(*p);
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            yield break;
                        }
                    }
                }

                if (*p == 'e' || *p == 'E')
                    goto on_exponent_start;

                if (*p != '.')
                {
                    // this is the end of the number
                    yield break;
                }

                // fallthrough

            on_mantissa_decimal:
                mantissa_.notify_decimal();
                if (consume())
                {
                    yield;
                    if (finalising())
                    {
                        yield break;
                    }
                }

                while (is_digit())
                {
                    mantissa_.notify_digit(*p);
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            yield break;
                        }
                    }
                }

                if (*p != 'e' && *p != 'E')
                {
                    yield break;
                }

            on_exponent_start:
                if (consume())
                {
                    yield;
                    if (finalising())
                    {
                        error_ = asio::error::invalid_argument;
                        yield break;
                    }
                }
                if (*p == '-')
                {
                    exponent_.notify_negative();
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            error_ = asio::error::invalid_argument;
                            yield break;
                        }
                    }
                }
                else if (*p == '+')
                {
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            error_ = asio::error::invalid_argument;
                            yield break;
                        }
                    }
                }
                while (is_digit())
                {
                    exponent_.notify_digit(*p);
                    if (consume())
                    {
                        yield;
                        if (finalising())
                        {
                            yield break;
                        }
                    }
                }
            }

            // special case if called with empty range, finalise values
            if (finalising())
            {
                mantissa_.finalise();
                exponent_.finalise();
            }

            return p;
        }
#include <boost/asio/unyield.hpp>

        void
        finalise()
        {
            static const char empty[] = "";
            if (!error_)
            {
                (*this)(empty, empty);
            }
        }

        number get_number() const { return number { mantissa_, exponent_ }; }

        mantissa_builder   mantissa_;
        exponent_builder   exponent_;
        system::error_code error_;
    };

    struct result
    {
        system::error_code ec;
        number             n;

        result(number_parser const& np)
        : ec(np.error())
        , n(np.get_number())
        {}

        auto
        as_tuple() const
        {
            return std::tie(ec, n);
        }

        friend std::ostream &
        operator<<(std::ostream &os, result const &r)
        {
            if (r.ec)
            {
                os << r.ec.category().name()<< " : " << r.ec.value() << " : " << r.ec.message();
            }
            else
            {
                os << r.n;
            }
            return os;
        }
    };

    bool
    operator==(result const &l, result const &r)
    {
        return l.as_tuple() == r.as_tuple();
    }

    bool
    operator!=(result const &l, result const &r)
    {
        return l.as_tuple() != r.as_tuple();
    }

    using namespace std::literals;

    struct grind_failure : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    result grind(std::string_view input)
    {
        number_parser np_base;
        auto next = np_base(input.begin(), input.end());
        if (!np_base.is_complete())
            np_base.finalise();
        auto consumed_base = std::distance(input.begin(), next);
        auto result_base = result(np_base);

        for (std::size_t i = 1 ; i < input.size() ; ++i)
        {
            number_parser np;
            next = np(input.data(), input.data() + i);
            if (!np.is_complete() && !np.error())
                next = np(next, input.data() + input.size());
           if (!np.is_complete())
               np.finalise();
            auto consumed = std::distance(input.begin(), next);
            auto res = result(np);

            if (res != result_base || consumed != consumed_base)
            {
                std::ostringstream ss;
                ss << "gind failure: "
                      "expected " << result_base << "," << consumed_base
                      << " but got " << res << "," << consumed;
                throw grind_failure(ss.str());
            }
        }
        return result_base;
    }

    int
    run()
    {
        using namespace std::literals;

        auto test_value = "100.0"sv;
        auto res = grind(test_value);
        std:: cout << test_value << "->" << res << std::endl;

        assert(!res.ec);

        return 0;
    }
}   // namespace program

int
main()
{
    try
    {
        return program::run();
    }
    catch (...)
    {
        std::cerr << program::explain() << std::endl;
        return 127;
    }
}