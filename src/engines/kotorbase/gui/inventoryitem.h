/* xoreos - A reimplementation of BioWare's Aurora engine
 *
 * xoreos is the legal property of its developers, whose names
 * can be found in the AUTHORS file distributed with this source
 * distribution.
 *
 * xoreos is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * xoreos is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with xoreos. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 *  Inventory item widget for KotOR games.
 */

#ifndef ENGINES_KOTORBASE_INVENTORYITEM_H
#define ENGINES_KOTORBASE_INVENTORYITEM_H

#include "src/engines/odyssey/protoitem.h"

namespace Engines {

namespace KotORBase {

class WidgetInventoryItem : public Odyssey::WidgetProtoItem {
public:
	WidgetInventoryItem(Engines::GUI &gui, const Common::UString &tag);

	void load(const Aurora::GFF3Struct &gff);

	// Basic visuals

	void show();
	void hide();


	void setContents(const Common::UString &contents);
	void setPosition(float x, float y, float z);
	void setWidth(float width);

private:
	std::unique_ptr<Graphics::Aurora::GUIQuad> _icon;
	std::unique_ptr<Graphics::Aurora::GUIQuad> _iconFrame;
	std::unique_ptr<Graphics::Aurora::Text>    _countText;
};

} // End of namespace KotORBase

} // End of namespace Engines

#endif // ENGINES_KOTORBASE_INVENTORYITEM_H
