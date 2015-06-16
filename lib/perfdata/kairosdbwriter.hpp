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

#ifndef KAIROSDBWRITER_H
#define KAIROSDBWRITER_H

#include "perfdata/kairosdbwriter.thpp"
#include "icinga/service.hpp"
#include "base/dynamicobject.hpp"
#include "base/tcpsocket.hpp"
#include "base/timer.hpp"
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <vector>

namespace icinga
{

/**
 * An Icinga KairosDB writer.
 *
 * @ingroup perfdata
 */
class KairosdbWriter : public ObjectImpl<KairosdbWriter>
{
public:
	DECLARE_OBJECT(KairosdbWriter);
	DECLARE_OBJECTNAME(KairosdbWriter);

	static void StatsFunc(const Dictionary::Ptr& status, const Array::Ptr& perfdata);

protected:
	virtual void Start(void);

private:
	Stream::Ptr m_Stream;

	Timer::Ptr m_ReconnectTimer;
	typedef std::vector< std::string > split_vector_type;

	void CheckResultHandler(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr);
	void SendMetric(const String& metric, double value, double ts, std::map<String, String>, const String& type);
	void SendPerfdata(const String& metric, const CheckResult::Ptr& cr, double ts, std::map<String, String>);
	static Value EscapeMacroMetric(const Value& value);
	String EscapeMetric(const String& str);
	void ReconnectTimerHandler(void);


};

}

#endif /* KAIROSDBWRITER_H */
