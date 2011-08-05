/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */


#include "asio_tcp.hpp"
#include "asio_udp.hpp"
#include "asio_addr.hpp"
#include "asio_protonet.hpp"

#include "socket.hpp"

#include "gcomm/util.hpp"
#include "gcomm/conf.hpp"

#include "gu_logger.hpp"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <fstream>

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;


#ifdef HAVE_ASIO_SSL_HPP

namespace
{
    static std::string
    get_file(const gu::Config& conf, const std::string& fname)
    {
        try
        {
            return conf.get(fname);
        }
        catch (gu::NotFound& e)
        {
            log_error << "could not find '" << fname << "' from configuration";
            throw;
        }
    }
}


std::string gcomm::AsioProtonet::get_ssl_password() const
{
    std::string   file(get_file(conf_, Conf::SocketSslPasswordFile));
    std::ifstream ifs(file.c_str(), ios_base::in);
    if (ifs.good() == false)
    {
        gu_throw_error(errno) << "could not open password file '" << file
                              << "'";
    }
    std::string ret;
    std::getline(ifs, ret);
    return ret;
}


#endif // HAVE_ASIO_SSL_HPP


gcomm::AsioProtonet::AsioProtonet(gu::Config& conf, int version)
    :
    gcomm::Protonet(conf, "asio", version),
    mutex_(),
    poll_until_(Date::max()),
    io_service_(),
    timer_(io_service_),
#ifdef HAVE_ASIO_SSL_HPP
    ssl_context_(io_service_, asio::ssl::context::sslv23),
#endif // HAVE_ASIO_SSL_HPP
    mtu_(1 << 15),
    checksum_(true)
{
#ifdef HAVE_ASIO_SSL_HPP
    if (gu::from_string<bool>(conf_.get(Conf::SocketUseSsl, "false")) == true)
    {
        log_info << "initializing ssl context";
        ssl_context_.set_verify_mode(asio::ssl::context::verify_peer);
        ssl_context_.set_password_callback(
            boost::bind(&gcomm::AsioProtonet::get_ssl_password, this));

        // verify file
        const std::string verify_file(
            get_file(conf_, Conf::SocketSslVerifyFile));
        try
        {
            ssl_context_.load_verify_file(verify_file);
        }
        catch (std::exception& e)
        {
            log_error << "could not load verify file '"
                      << verify_file
                      << "': " << e.what();
            throw;
        }

        // certificate file
        const std::string certificate_file(
            get_file(conf_, Conf::SocketSslCertificateFile));
        try
        {
            ssl_context_.use_certificate_file(certificate_file,
                                              asio::ssl::context::pem);
        }
        catch (std::exception& e)
        {
            log_error << "could not load certificate file'"
                      << certificate_file
                      << "': " << e.what();
            throw;
        }

        // private key file
        const std::string private_key_file(
            get_file(conf_, Conf::SocketSslPrivateKeyFile));
        try
        {
            ssl_context_.use_private_key_file(
                private_key_file, asio::ssl::context::pem);
        }
        catch (gu::NotFound& e)
        {
            log_error << "could not load private key file '"
                      << private_key_file << "'";
            throw;
        }
        catch (std::exception& e)
        {
            log_error << "could not use private key file '"
                      << private_key_file
                      << "': " << e.what();
            throw;
        }
    }
#endif // HAVE_ASIO_SSL_HPP
}

gcomm::AsioProtonet::~AsioProtonet()
{

}

void gcomm::AsioProtonet::enter()
{
    mutex_.lock();
}



void gcomm::AsioProtonet::leave()
{
    mutex_.unlock();
}

gcomm::SocketPtr gcomm::AsioProtonet::socket(const URI& uri)
{
    if (uri.get_scheme() == "tcp" || uri.get_scheme() == "ssl")
    {
        return boost::shared_ptr<AsioTcpSocket>(new AsioTcpSocket(*this, uri));
    }
    else if (uri.get_scheme() == "udp")
    {
        return boost::shared_ptr<AsioUdpSocket>(new AsioUdpSocket(*this, uri));
    }
    else
    {
        gu_throw_fatal << "scheme '" << uri.get_scheme() << "' not implemented";
        throw;
    }
}

gcomm::Acceptor* gcomm::AsioProtonet::acceptor(const URI& uri)
{
    return new AsioTcpAcceptor(*this, uri);
}



Period handle_timers_helper(gcomm::Protonet& pnet, const Period& period)
{
    const Date now(Date::now());
    const Date stop(now + period);

    const Date next_time(pnet.handle_timers());
    const Period sleep_p(min(stop - now, next_time - now));
    return (sleep_p < 0 ? 0 : sleep_p);
}


void gcomm::AsioProtonet::event_loop(const Period& period)
{
    io_service_.reset();
    poll_until_ = Date::now() + period;

    const Period p(handle_timers_helper(*this, period));
    timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
    timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                  asio::placeholders::error));
    io_service_.run();
}


void gcomm::AsioProtonet::dispatch(const SocketId& id,
                                   const Datagram& dg,
                                   const ProtoUpMeta& um)
{
    for (deque<Protostack*>::iterator i = protos_.begin();
         i != protos_.end(); ++i)
    {
        (*i)->dispatch(id, dg, um);
    }
}


void gcomm::AsioProtonet::interrupt()
{
    io_service_.stop();
}


void gcomm::AsioProtonet::handle_wait(const asio::error_code& ec)
{
    Date now(Date::now());
    const Period p(handle_timers_helper(*this, poll_until_ - now));
    if (ec == asio::error_code() && poll_until_ >= now)
    {
        timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
        timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                      asio::placeholders::error));
    }
    else
    {
        io_service_.stop();
    }
}

