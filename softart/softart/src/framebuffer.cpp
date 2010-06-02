/*
Copyright (C) 2007-2010 Minmin Gong, Ye Wu

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "eflib/include/util.h"

#include "../include/framebuffer.h"
#include "../include/surface.h"
#include "../include/renderer_impl.h"
#include "../include/shader.h"

BEGIN_NS_SOFTART()


using namespace efl;
using namespace std;

/****************************************************
 *   Framebuffer
 ***************************************************/
bool framebuffer::check_buf(surface* psurf){
	return psurf && (psurf->get_width() < width_ || psurf->get_height() < height_);
}

void framebuffer::initialize(renderer_impl* pparent)
{
	pparent_ = pparent;
}

framebuffer::framebuffer(size_t width, size_t height, pixel_format fmt)
:width_(width), height_(height), fmt_(fmt),
back_cbufs_(pso_color_regcnt), cbufs_(pso_color_regcnt), buf_valids(pso_color_regcnt),
dbuf_(new surface(width, height,  pixel_format_color_r32f)),
sbuf_(new surface(width, height,  pixel_format_color_r32i))
{
	for(size_t i = 0; i < back_cbufs_.size(); ++i){
		back_cbufs_[i].reset();
	}

	back_cbufs_[0].reset(new surface(width, height, fmt));

	for(size_t i = 0; i < cbufs_.size(); ++i){
		cbufs_[i] = back_cbufs_[i].get();
	}

	buf_valids[0] = true;
	for(size_t i = 1; i < buf_valids.size(); ++i){
		buf_valids[i] = false;
	}
}

framebuffer::~framebuffer(void)
{
}

//重置，第一个RT将设置为帧缓冲表面，其它RT置空
void framebuffer::reset(size_t width, size_t height, pixel_format fmt)
{
	new(this) framebuffer(width, height, fmt);
}

void framebuffer::set_render_target_disabled(render_target tar, size_t tar_id){
	custom_assert(tar == render_target_color, "只能禁用颜色缓冲");
	custom_assert(tar_id < cbufs_.size(), "颜色缓冲ID的设置错误");
	UNREF_PARAM(tar);

	//简单的设置为无效
	buf_valids[tar_id] = false;
}

void framebuffer::set_render_target_enabled(render_target tar, size_t tar_id){
	custom_assert(tar == render_target_color, "只能启用颜色缓冲");
	custom_assert(tar_id < cbufs_.size(), "颜色缓冲ID的设置错误");
	UNREF_PARAM(tar);

	//重分配后缓冲
	if(back_cbufs_[tar_id] && check_buf(back_cbufs_[tar_id].get())){
		back_cbufs_[tar_id].reset(new surface(width_, height_, fmt_));
	}

	//如果渲染目标为空则自动挂接后缓冲
	if(cbufs_[tar_id] == NULL){
		cbufs_[tar_id] = back_cbufs_[tar_id].get();
	}

	//检测后缓冲有效性
	if(check_buf(cbufs_[tar_id])){
		custom_assert(false, "目标缓冲无效！");
		cbufs_[tar_id] = back_cbufs_[tar_id].get();
	}

	//设置有效性
	buf_valids[tar_id] = true;
}

//渲染目标设置。暂时不支持depth buffer和stencil buffer的绑定和读取。
void framebuffer::set_render_target(render_target tar, size_t tar_id, surface* psurf)
{
	custom_assert(tar == render_target_color, "只能绑定颜色缓冲");
	custom_assert(tar_id < cbufs_.size(), "颜色缓冲ID的绑定错误");
	UNREF_PARAM(tar);

	//如果传入的表面为空则恢复渲染目标为后备缓冲
	if(!psurf){
		cbufs_[tar_id] = back_cbufs_[tar_id].get();
		return;
	}

	custom_assert(psurf->get_width() >= width_ && psurf->get_height() >= height_, "渲染目标的大小不足");
	cbufs_[tar_id] = psurf;
}

surface* framebuffer::get_render_target(render_target tar, size_t tar_id) const
{
	custom_assert(tar == render_target_color, "只能获得颜色缓冲");
	custom_assert(tar_id < cbufs_.size(), "颜色缓冲ID设置错误");
	UNREF_PARAM(tar);

	return cbufs_[tar_id];
}
	
rect<size_t> framebuffer::get_rect(){
		return rect<size_t>(0, 0, width_, height_);
	}

size_t framebuffer::get_width() const{
	return width_;
}

size_t framebuffer::get_height() const{
	return height_;
}

pixel_format framebuffer::get_buffer_format() const{
	return fmt_;
}

//渲染
void framebuffer::render_pixel(const h_blend_shader& hbs, size_t x, size_t y, const ps_output& ps)
{
	custom_assert(hbs, "未设置Target Shader!");
	if(! hbs) return;

	//composing output...
	backbuffer_pixel_out target_pixel(cbufs_, dbuf_.get(), sbuf_.get());
	target_pixel.set_pos(x, y);

	// TODO: depth stencil state object
	if (ps.depth < target_pixel.depth())
	{
		//execute target shader
		hbs->execute(target_pixel, ps);
		target_pixel.depth(ps.depth);
	}
}

void framebuffer::clear_color(size_t tar_id, const color_rgba32f& c){

	custom_assert(tar_id < cbufs_.size(), "渲染目标标识设置错误！");
	custom_assert(cbufs_[tar_id] && buf_valids[tar_id], "试图对一个无效的渲染目标设置颜色！");

	cbufs_[tar_id]->fill_texels(0, 0, width_, height_, c);
}

void framebuffer::clear_depth(float d){
	dbuf_->fill_texels(0, 0, width_, height_, color_rgba32f(d, 0, 0, 0));
}

void framebuffer::clear_stencil(int32_t s){
	sbuf_->fill_texels(0, 0, width_, height_, color_rgba32f(float(s), 0, 0, 0));
}

void framebuffer::clear_color(size_t tar_id, const rect<size_t>& rc, const color_rgba32f& c){

	custom_assert(tar_id < cbufs_.size(), "渲染目标标识设置错误！");
	custom_assert(cbufs_[tar_id] && buf_valids[tar_id], "试图对一个无效的渲染目标设置颜色！");
	custom_assert(rc.w + rc.x <= width_ && rc.h +rc.y <= height_, "锁定区域超过了帧缓冲范围！");

	cbufs_[tar_id]->fill_texels(rc.x, rc.y, rc.w, rc.h, c);
}

void framebuffer::clear_depth(const rect<size_t>& rc, float d){
	custom_assert(rc.w + rc.x <= width_ && rc.h +rc.y <= height_, "锁定区域超过了帧缓冲范围！");

	dbuf_->fill_texels(rc.x, rc.y, rc.w, rc.h, color_rgba32f(d, 0, 0, 0));
}

void framebuffer::clear_stencil(const rect<size_t>& rc, int32_t s){
	custom_assert(rc.w + rc.x <= width_ && rc.h +rc.y <= height_, "锁定区域超过了帧缓冲范围！");

	sbuf_->fill_texels(rc.x, rc.y, rc.w, rc.h, color_rgba32f(float(s), 0, 0, 0));
}

END_NS_SOFTART()
