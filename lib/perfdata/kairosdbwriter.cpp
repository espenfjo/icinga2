/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2015 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "perfdata/kairosdbwriter.hpp"
#include "perfdata/kairosdbwriter.tcpp"
#include "icinga/service.hpp"
#include "icinga/macroprocessor.hpp"
#include "icinga/icingaapplication.hpp"
#include "icinga/compatutility.hpp"
#include "icinga/perfdatavalue.hpp"
#include "base/tcpsocket.hpp"
#include "base/dynamictype.hpp"
#include "base/objectlock.hpp"
#include "base/array.hpp"
#include "base/logger.hpp"
#include "base/convert.hpp"
#include "base/utility.hpp"
#include "base/application.hpp"
#include "base/stream.hpp"
#include "base/networkstream.hpp"
#include "base/exception.hpp"
#include "base/statsfunction.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/assign/list_of.hpp>
#include <list>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/fusion/adapted/std_pair.hpp>
using namespace icinga;

REGISTER_TYPE(KairosdbWriter);

REGISTER_STATSFUNCTION(KairosdbWriterStats, &KairosdbWriter::StatsFunc);

void KairosdbWriter::StatsFunc(const Dictionary::Ptr& status, const Array::Ptr&)
{
	Dictionary::Ptr nodes = new Dictionary();

	BOOST_FOREACH(const KairosdbWriter::Ptr& kairosdbwriter, DynamicType::GetObjectsByType<KairosdbWriter>()) {
		nodes->Set(kairosdbwriter->GetName(), 1); //add more stats
	}

	status->Set("kairosdbwriter", nodes);
}

void KairosdbWriter::Start(void)
{
	DynamicObject::Start();

	m_ReconnectTimer = new Timer();
	m_ReconnectTimer->SetInterval(10);
	m_ReconnectTimer->OnTimerExpired.connect(boost::bind(&KairosdbWriter::ReconnectTimerHandler, this));
	m_ReconnectTimer->Start();
	m_ReconnectTimer->Reschedule(0);

	Service::OnNewCheckResult.connect(boost::bind(&KairosdbWriter::CheckResultHandler, this, _1, _2));
}

void KairosdbWriter::ReconnectTimerHandler(void)
{
	if (m_Stream)
		return;

	TcpSocket::Ptr socket = new TcpSocket();

	Log(LogNotice, "KairosdbWriter")
		<< "Reconnecting to KairosDB on host '" << GetHost() << "' port '" << GetPort() << "'.";

	try {
		socket->Connect(GetHost(), GetPort());
	} catch (std::exception&) {
		Log(LogCritical, "KairosDBWriter")
			<< "Can't connect to KairosDB on host '" << GetHost() << "' port '" << GetPort() << "'.";
		return;
	}

	m_Stream = new NetworkStream(socket);
}

void KairosdbWriter::CheckResultHandler(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr)
{
	CONTEXT("Processing check result for '" + checkable->GetName() + "'");

	if (!IcingaApplication::GetInstance()->GetEnablePerfdata() || !checkable->GetEnablePerfdata())
		return;

	Service::Ptr service = dynamic_pointer_cast<Service>(checkable);
	Host::Ptr host;

	if (service)
		host = service->GetHost();
	else
		host = static_pointer_cast<Host>(checkable);

	String hostName = host->GetName();

	MacroProcessor::ResolverList resolvers;
	if (service)
		resolvers.push_back(std::make_pair("service", service));
	resolvers.push_back(std::make_pair("host", host));
	resolvers.push_back(std::make_pair("icinga", IcingaApplication::GetInstance()));

	String metric;
	std::map<String, String> tags;
	double ts = cr->GetExecutionEnd();
	tags["host"] = hostName;
	tags["source"] = Utility::GetFQDN();
	tags["target"] = "meta";
	if (service) {
		metric = MacroProcessor::ResolveMacros("$service.vars.kairosdb_metric_name$", resolvers, cr, NULL, &KairosdbWriter::EscapeMacroMetric);
		Log(LogWarning, "KairosdbWriter") << "Working on service: '" << service->GetShortName() << "' metric name: '" << metric << "'.";

		Value raw_config_tags = MacroProcessor::ResolveMacros("$service.vars.kairosdb_tags$", resolvers, cr, NULL, &KairosdbWriter::EscapeMacroMetric);
		if (raw_config_tags != 0) {
			if (!raw_config_tags.IsObjectType<Array>())
				Log(LogWarning, "KairosdbWriter") << "Service " << service->GetShortName() << " not an array'";

			Array::Ptr vars_tags;
			vars_tags = raw_config_tags;
			BOOST_FOREACH(String tag, vars_tags) {
				std::vector< std::string > vars_tag;
				boost::split(vars_tag, tag, boost::is_any_of("="));
				if (vars_tag[1] != "")
					tags[vars_tag[0]] = vars_tag[1];
			}
		}
		if (metric == "")
			metric = service->GetShortName();
		SendMetric(metric, service->GetState(), ts, tags, "state");
	} else {
		metric = hostName;
		Log(LogWarning, "KairosdbWriter") << "Working on host: " << metric;
		Value raw_config_tags = MacroProcessor::ResolveMacros("$service.vars.kairosdb_tags$", resolvers, cr, NULL, &KairosdbWriter::EscapeMacroMetric);
		if (raw_config_tags != 0) {
			if (!raw_config_tags.IsObjectType<Array>())
				Log(LogWarning, "KairosdbWriter") << "Service " << service->GetShortName() << " not an array'";

			Array::Ptr vars_tags;
			vars_tags = raw_config_tags;
			BOOST_FOREACH(String tag, vars_tags) {
				std::vector< std::string > vars_tag;
				boost::split(vars_tag, tag, boost::is_any_of("="));
				if (vars_tag[1] != "")
					tags[vars_tag[0]] = vars_tag[1];
			}
		}
		SendMetric(metric, host->GetState(), ts, tags, "state");
	}

	SendMetric(metric, checkable->GetCheckAttempt(), ts, tags, "current_attempt");
	SendMetric(metric, checkable->GetMaxCheckAttempts(), ts, tags, "max_check_attempts");
	SendMetric(metric, checkable->GetStateType(), ts, tags, "state_type");
	SendMetric(metric, checkable->IsReachable(), ts, tags, "reachable");
	SendMetric(metric, checkable->GetDowntimeDepth(), ts, tags, "downtime_depth");
	SendMetric(metric, Service::CalculateLatency(cr), ts, tags, "latency");
	SendMetric(metric, Service::CalculateExecutionTime(cr), ts, tags, "execution_time");
	SendPerfdata(metric, cr, ts, tags);
}

void KairosdbWriter::SendPerfdata(const String& metric, const CheckResult::Ptr& cr, double ts,  std::map<String, String> tags)
{
	Array::Ptr perfdata = cr->GetPerformanceData();

	if (!perfdata)
		return;

	ObjectLock olock(perfdata);
	BOOST_FOREACH(const Value& val, perfdata) {
		PerfdataValue::Ptr pdv;

		if (val.IsObjectType<PerfdataValue>())
			pdv = val;
		else {
			try {
				pdv = PerfdataValue::Parse(val);
			} catch (const std::exception&) {
				Log(LogWarning, "KairosdbWriter")
					<< "Ignoring invalid perfdata value: " << val;
				continue;
			}
		}

		tags["target"] = pdv->GetLabel();
		SendMetric(metric, pdv->GetValue(), ts, tags, "value");
		if (pdv->GetCrit())
			SendMetric(metric, pdv->GetCrit(), ts, tags, "crit");
		if (pdv->GetWarn())
			SendMetric(metric, pdv->GetWarn(), ts, tags, "warn");
		if (pdv->GetMin())
			SendMetric(metric, pdv->GetMin(), ts, tags, "min");
		if (pdv->GetMax())
			SendMetric(metric, pdv->GetMax(), ts, tags, "max");
	}
}

void KairosdbWriter::SendMetric(const String& metric, double value, double ts, std::map<String, String> tags, const String& type )
{

	if (type != "")
		tags["type"] = type;

	String tags_string = "";
	BOOST_FOREACH(const Dictionary::Pair& tag, tags) {
		tags_string += " " + tag.first + "=" + Convert::ToString(tag.second);
	}

	/*
	 * must be (https://kairosdb.github.io/kairosdocs/telnetapi/Put.html)
	 * put <metric> <timestamp> <value> <tagk1=tagv1[ tagk2=tagv2 ...tagkN=tagvN]>
	 * "tags" should include at least one tag, we use "host=HOSTNAME"
	 */
	std::ostringstream msgbuf;
	msgbuf << "put " << metric << " " << static_cast<long>(ts) << " " << Convert::ToString(value) << tags_string;;

	Log(LogDebug, "KairosdbWriter")
		<< "Added to metric list: '" << msgbuf.str() << "'.";

	// do not send \n to debug log
	msgbuf << "\n";
	String msg = msgbuf.str();

	ObjectLock olock(this);

	if (!m_Stream)
		return;

	try {
		m_Stream->Write(msg.CStr(), msg.GetLength());
	} catch (const std::exception& ex) {
		Log(LogCritical, "KairosdbWriter")
			<< "Cannot write to TCP socket on host '" << GetHost() << "' port '" << GetPort() << "'.";

		m_Stream.reset();
	}
}

String KairosdbWriter::EscapeMetric(const String& str)
{
	String result = str;

	boost::replace_all(result, " ", "_");
	boost::replace_all(result, "\\", "_");
	boost::replace_all(result, "/", "_");

	return result;
}

Value KairosdbWriter::EscapeMacroMetric(const Value& value)
{
    return value;
	if (value.IsObjectType<Array>()) {
		Array::Ptr arr = value;
		Array::Ptr result = new Array();

		ObjectLock olock(arr);
		BOOST_FOREACH(const Value& arg, arr) {
			result->Add(arg);
		}

		return Utility::Join(result, '.');
	} else
		return value;
}
